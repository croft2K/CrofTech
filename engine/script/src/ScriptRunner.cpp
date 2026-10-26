#include "croftech/script/ScriptRunner.h"

#include "ScriptArena.h"
#include "ScriptSandbox.h"

#include "croftech/core/Random.h"
#include "croftech/script/ScriptProgram.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <numbers>
#include <optional>

namespace ct {
namespace {

constexpr std::string_view kMathNames[] = {"abs", "ceil", "clamp", "floor", "fmod", "huge", "lerp",
                                           "max", "min",  "pi",    "round", "sign", "sqrt"};

constexpr std::string_view kMissingGlobals[] = {
    "collectgarbage", "debug",      "dofile",   "gcinfo", "getfenv", "io",
    "load",           "loadstring", "newproxy", "os",     "require", "setfenv",
};

template <std::size_t N>
void append(FixedText<N>& text, std::string_view more) {
    const std::size_t count = std::min(more.size(), N - text.length);
    std::copy_n(more.data(), count, text.characters.data() + text.length);
    text.length += static_cast<std::uint32_t>(count);
}

template <std::size_t N>
void assign(FixedText<N>& text, std::string_view value) {
    text.length = 0;
    append(text, value);
}

// A name of a state value: a Luau identifier, so game.state.name works.
bool isValidStateName(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength || (name[0] >= '0' && name[0] <= '9')) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    });
}

// The maths library. Every function here is exact (IEEE 754 requires these results to be
// correctly rounded, or they involve no rounding at all), so it gives the same everywhere.
int mathAbs(lua_State* L) {
    lua_pushnumber(L, std::fabs(luaL_checknumber(L, 1)));
    return 1;
}

int mathCeil(lua_State* L) {
    lua_pushnumber(L, std::ceil(luaL_checknumber(L, 1)));
    return 1;
}

int mathFloor(lua_State* L) {
    lua_pushnumber(L, std::floor(luaL_checknumber(L, 1)));
    return 1;
}

int mathSqrt(lua_State* L) {
    lua_pushnumber(L, std::sqrt(luaL_checknumber(L, 1)));
    return 1;
}

int mathFmod(lua_State* L) {
    lua_pushnumber(L, std::fmod(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

// Halfway rounds away from zero, as Luau's own does.
int mathRound(lua_State* L) {
    lua_pushnumber(L, std::round(luaL_checknumber(L, 1)));
    return 1;
}

int mathSign(lua_State* L) {
    const double x = luaL_checknumber(L, 1);
    lua_pushnumber(L, x > 0.0 ? 1.0 : x < 0.0 ? -1.0 : 0.0);
    return 1;
}

int mathClamp(lua_State* L) {
    const double x = luaL_checknumber(L, 1);
    const double low = luaL_checknumber(L, 2);
    const double high = luaL_checknumber(L, 3);
    if (low > high) {
        luaL_error(L, "math.clamp: the minimum is above the maximum");
    }
    lua_pushnumber(L, x < low ? low : x > high ? high : x);
    return 1;
}

int mathLerp(lua_State* L) {
    const double a = luaL_checknumber(L, 1);
    const double b = luaL_checknumber(L, 2);
    const double t = luaL_checknumber(L, 3);
    lua_pushnumber(L, t == 1.0 ? b : a + (b - a) * t);
    return 1;
}

template <bool Max>
int mathExtreme(lua_State* L) {
    double best = luaL_checknumber(L, 1);
    const int count = lua_gettop(L);
    for (int i = 2; i <= count; ++i) {
        const double value = luaL_checknumber(L, i);
        if (Max ? value > best : value < best) {
            best = value;
        }
    }
    lua_pushnumber(L, best);
    return 1;
}

void openMath(lua_State* L) {
    constexpr luaL_Reg kFunctions[] = {
        {"abs", mathAbs},           {"ceil", mathCeil},          {"clamp", mathClamp},
        {"floor", mathFloor},       {"fmod", mathFmod},          {"lerp", mathLerp},
        {"max", mathExtreme<true>}, {"min", mathExtreme<false>}, {"round", mathRound},
        {"sign", mathSign},         {"sqrt", mathSqrt},          {nullptr, nullptr},
    };
    luaL_register(L, "math", kFunctions);
    lua_pushnumber(L, std::numbers::pi);
    lua_setfield(L, -2, "pi");
    lua_pushnumber(L, std::numeric_limits<double>::infinity());
    lua_setfield(L, -2, "huge");
    lua_pop(L, 1);
}

// Whichever way a game function is called, game:fire(x) or game.fire(x), the first argument after
// the game.
int firstArgument(lua_State* L) { return lua_istable(L, 1) ? 2 : 1; }

struct ProtectedRun {
    std::string_view name;
    std::string_view bytecode;
    void (*body)(lua_State*, void*) = nullptr;
    void* data = nullptr;
};

} // namespace

std::span<const std::string_view> scriptMathNames() { return kMathNames; }

std::span<const std::string_view> scriptMissingGlobals() { return kMissingGlobals; }

class ScriptSandbox {
public:
    static ScriptRunner& runner(lua_State* L) {
        return *static_cast<ScriptRunner*>(lua_callbacks(L)->userdata);
    }

    static void* allocate(void* data, void* block, std::size_t oldSize, std::size_t newSize) {
        return static_cast<ScriptRunner*>(data)->reallocate(block, oldSize, newSize);
    }

    // Called at Luau's safepoints; gc is -1 for those in running code.
    static void interrupt(lua_State* L, int gc) {
        if (gc < 0) {
            runner(L).countStep(L);
        }
    }

    // The program of the call running, which game functions need; there is none while a script's
    // declarations are read, when no game exists to call them on, but a script could keep the
    // functions somewhere odd.
    static const ScriptProgram& program(lua_State* L) {
        const ScriptCall* call = runner(L).m_call;
        if (call == nullptr) {
            luaL_error(L, "game functions work only in a call of the script");
        }
        return *call->program;
    }

    static bool isReference(int type) {
        return type == LUA_TTABLE || type == LUA_TFUNCTION || type == LUA_TUSERDATA ||
               type == LUA_TTHREAD || type == LUA_TBUFFER || type == LUA_TLIGHTUSERDATA;
    }

    // The value as text, as tostring gives it, except that a table, function or the like without
    // a __tostring shows where it is in the call's memory rather than its address, whose high bits
    // differ from process to process.
    static const char* pushText(lua_State* L, int index, std::size_t* length) {
        if (isReference(lua_type(L, index))) {
            if (luaL_getmetafield(L, index, "__tostring") == 0) {
                char text[64];
                std::snprintf(text, sizeof(text), "%s: %llx", luaL_typename(L, index),
                              static_cast<unsigned long long>(
                                  runner(L).m_arena->offsetOf(lua_topointer(L, index))));
                lua_pushstring(L, text);
                return lua_tolstring(L, -1, length);
            }
            lua_pop(L, 1);
        }
        return luaL_tolstring(L, index, length);
    }

    static int sandboxToString(lua_State* L) {
        luaL_checkany(L, 1);
        pushText(L, 1, nullptr);
        return 1;
    }

    // string.format with the original as its upvalue, given references as pushText shows them.
    static int sandboxFormat(lua_State* L) {
        const int count = lua_gettop(L);
        for (int i = 2; i <= count; ++i) {
            if (isReference(lua_type(L, i))) {
                pushText(L, i, nullptr);
                lua_replace(L, i);
            }
        }
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, 1);
        return 1;
    }

    static int sandboxPrint(lua_State* L) {
        FixedText<kMaxScriptPrintLength> line;
        const int count = lua_gettop(L);
        for (int i = 1; i <= count; ++i) {
            std::size_t length = 0;
            const char* text = pushText(L, i, &length);
            if (i > 1) {
                append(line, " ");
            }
            append(line, {text, length});
            lua_pop(L, 1);
        }
        runner(L).print(line.view());
        return 0;
    }

    static int gameFire(lua_State* L) {
        ScriptRunner& owner = runner(L);
        const char* event = luaL_checkstring(L, firstArgument(L));
        const std::optional<std::uint32_t> index = program(L).findEvent(event);
        if (!index) {
            luaL_error(L, "game:fire: no event '%s' (declare it in events)", event);
        }
        ScriptEffect effect;
        effect.kind = ScriptEffectKind::Fire;
        effect.index = *index;
        owner.addEffect(L, effect);
        return 0;
    }

    static double checkDelay(lua_State* L, int argument, double seconds) {
        if (!(seconds >= 0.0 && seconds <= static_cast<double>(kMaxOutputDelay))) {
            luaL_error(L, "argument %d: a delay is 0 to %g seconds", argument,
                       static_cast<double>(kMaxOutputDelay));
        }
        return seconds;
    }

    static int gameAfter(lua_State* L) {
        ScriptRunner& owner = runner(L);
        const int first = firstArgument(L);
        const double seconds = checkDelay(L, first, luaL_checknumber(L, first));
        const char* input = luaL_checkstring(L, first + 1);
        const std::optional<std::uint32_t> index = program(L).findInput(input);
        if (!index) {
            luaL_error(L, "game:after: no input '%s' (declare it in inputs)", input);
        }
        ScriptEffect effect;
        effect.kind = ScriptEffectKind::After;
        effect.index = *index;
        effect.delay = seconds;
        owner.addEffect(L, effect);
        return 0;
    }

    static int gameSend(lua_State* L) {
        ScriptRunner& owner = runner(L);
        program(L);
        const int first = firstArgument(L);
        const char* target = luaL_checkstring(L, first);
        const char* input = luaL_checkstring(L, first + 1);
        const double seconds = checkDelay(L, first + 2, luaL_optnumber(L, first + 2, 0.0));
        if (!isValidEntityName(target) || !isValidEntityName(input)) {
            luaL_error(L,
                       "game:send: '%s' and '%s' must be names: letters, digits, '_', '.' or "
                       "'-'",
                       target, input);
        }
        ScriptEffect effect;
        effect.kind = ScriptEffectKind::Send;
        effect.delay = seconds;
        assign(effect.target, target);
        assign(effect.targetInput, input);
        owner.addEffect(L, effect);
        return 0;
    }

    static int gameRandom(lua_State* L) {
        program(L);
        const int first = firstArgument(L);
        const double unit = runner(L).nextRandom();
        if (lua_isnoneornil(L, first)) {
            lua_pushnumber(L, unit);
            return 1;
        }
        const double low = luaL_checknumber(L, first);
        const double high = luaL_checknumber(L, first + 1);
        if (low != std::floor(low) || high != std::floor(high) || !(low <= high) ||
            high - low >= 2147483648.0) {
            luaL_error(L, "game:random(m, n) takes whole numbers with m <= n");
        }
        lua_pushnumber(L, low + std::floor(unit * (high - low + 1.0)));
        return 1;
    }

    static void openLibraries(lua_State* L) {
        for (const lua_CFunction open :
             {luaopen_base, luaopen_coroutine, luaopen_table, luaopen_string, luaopen_bit32,
              luaopen_buffer, luaopen_utf8}) {
            lua_pushcfunction(L, open, nullptr);
            lua_call(L, 0, 0);
        }
        for (const std::string_view name : kMissingGlobals) {
            lua_pushnil(L);
            lua_setglobal(L, name.data());
        }
        lua_pushcfunction(L, sandboxPrint, "print");
        lua_setglobal(L, "print");
        lua_pushcfunction(L, sandboxToString, "tostring");
        lua_setglobal(L, "tostring");
        lua_getglobal(L, "string");
        lua_getfield(L, -1, "format");
        lua_pushcclosure(L, sandboxFormat, "format", 1);
        lua_setfield(L, -2, "format");
        lua_pop(L, 1);
        openMath(L);
    }

    static int runProtected(lua_State* L) {
        const auto* run = static_cast<const ProtectedRun*>(lua_touserdata(L, 1));
        openLibraries(L);
        // "=name" makes errors read "name:line: message".
        char chunkName[kMaxNameLength + 2] = "=";
        const std::size_t length = std::min(run->name.size(), kMaxNameLength);
        std::copy_n(run->name.data(), length, chunkName + 1);
        chunkName[length + 1] = '\0';
        if (luau_load(L, chunkName, run->bytecode.data(), run->bytecode.size(), 0) != 0) {
            lua_error(L);
        }
        lua_call(L, 0, 1);
        if (!lua_istable(L, -1)) {
            luaL_error(L, "%s must return a table (of state, events, inputs and start, or decide)",
                       chunkName + 1);
        }
        run->body(L, run->data);
        return 0;
    }

    // The game table for a call, on top of the stack.
    static void pushGame(lua_State* L, const ScriptCall& call) {
        const ScriptDeclarations& declared = call.program->declarations();
        lua_createtable(L, 0, 6);
        lua_createtable(L, 0, static_cast<int>(declared.state.size()));
        for (std::size_t i = 0; i < declared.state.size(); ++i) {
            if (declared.state[i].flag) {
                lua_pushboolean(L, call.state[i] != 0.0);
            } else {
                lua_pushnumber(L, call.state[i]);
            }
            lua_setfield(L, -2, declared.state[i].name.c_str());
        }
        lua_setfield(L, -2, "state");
        lua_pushnumber(L, call.time);
        lua_setfield(L, -2, "time");
        lua_pushcfunction(L, gameFire, "fire");
        lua_setfield(L, -2, "fire");
        lua_pushcfunction(L, gameAfter, "after");
        lua_setfield(L, -2, "after");
        lua_pushcfunction(L, gameSend, "send");
        lua_setfield(L, -2, "send");
        lua_pushcfunction(L, gameRandom, "random");
        lua_setfield(L, -2, "random");
    }

    static int subjectSetting(lua_State* L) {
        const ScriptCall* call = runner(L).m_call;
        const char* name = luaL_checkstring(L, firstArgument(L));
        if (call != nullptr) {
            for (const ScriptValue& value : call->settings) {
                if (value.name == name) {
                    lua_pushnumber(L, value.x);
                    return 1;
                }
            }
        }
        luaL_error(L, "me:setting: there is no setting '%s'", name);
        return 0;
    }

    // The values as a table, on top of the stack.
    static void pushValues(lua_State* L, std::span<const ScriptValue> values, int extra) {
        lua_createtable(L, 0, static_cast<int>(values.size()) + extra);
        for (const ScriptValue& value : values) {
            switch (value.kind) {
            case ScriptValueKind::Number:
                lua_pushnumber(L, value.x);
                break;
            case ScriptValueKind::Flag:
                lua_pushboolean(L, value.x != 0.0);
                break;
            case ScriptValueKind::Text:
                lua_pushlstring(L, value.text.data(), value.text.size());
                break;
            case ScriptValueKind::Vector:
                lua_createtable(L, 0, 3);
                lua_pushnumber(L, value.x);
                lua_setfield(L, -2, "x");
                lua_pushnumber(L, value.y);
                lua_setfield(L, -2, "y");
                lua_pushnumber(L, value.z);
                lua_setfield(L, -2, "z");
                break;
            }
            // lua_setfield wants the name ended; the caller's names are short.
            char name[64] = {};
            std::copy_n(value.name.data(), std::min(value.name.size(), sizeof(name) - 1), name);
            lua_setfield(L, -2, name);
        }
    }

    // A decide call's me, on top of the stack.
    static void pushSubject(lua_State* L, const ScriptCall& call) {
        pushValues(L, call.subject, 2);
        if (!call.target.empty()) {
            pushValues(L, call.target, 0);
            lua_setfield(L, -2, "target");
        }
        lua_pushcfunction(L, subjectSetting, "setting");
        lua_setfield(L, -2, "setting");
    }

    // The values of game.state (on top of the stack) after a call, which must be the declared
    // ones and no others, of their declared kinds.
    static void readState(lua_State* L, const ScriptDeclarations& declared, double* values) {
        if (!lua_istable(L, -1)) {
            luaL_error(L, "game.state must stay a table");
        }
        const int state = lua_gettop(L);
        for (std::size_t i = 0; i < declared.state.size(); ++i) {
            const ScriptVariable& variable = declared.state[i];
            lua_rawgetfield(L, state, variable.name.c_str());
            if (variable.flag) {
                if (!lua_isboolean(L, -1)) {
                    luaL_error(L, "state.%s must be true or false", variable.name.c_str());
                }
                values[i] = lua_toboolean(L, -1) ? 1.0 : 0.0;
            } else {
                const double value = lua_tonumber(L, -1);
                if (lua_type(L, -1) != LUA_TNUMBER || !std::isfinite(value)) {
                    luaL_error(L, "state.%s must be a number", variable.name.c_str());
                }
                values[i] = value;
            }
            lua_pop(L, 1);
        }
        lua_pushnil(L);
        while (lua_next(L, state) != 0) {
            lua_pop(L, 1);
            const char* name = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
            const bool known = name != nullptr &&
                               std::any_of(declared.state.begin(), declared.state.end(),
                                           [&](const ScriptVariable& v) { return v.name == name; });
            if (!known) {
                luaL_error(L, "state.%s isn't declared: list every value it keeps in state",
                           name != nullptr ? name : "?");
            }
        }
    }

    struct CallResult {
        std::array<double, kMaxScriptState> values{};
        bool decision = false;
    };

    static void callBody(lua_State* L, void* data) {
        const ScriptCall& call = *runner(L).m_call;
        const ScriptDeclarations& declared = call.program->declarations();
        const int script = lua_gettop(L);
        pushGame(L, call);
        const int game = lua_gettop(L);
        const char* function = "start";
        if (call.input == kScriptStart) {
            lua_rawgetfield(L, script, "start");
        } else if (call.input == kScriptDecide) {
            function = "decide";
            lua_rawgetfield(L, script, "decide");
        } else {
            function = declared.inputs[call.input].c_str();
            lua_rawgetfield(L, script, "inputs");
            if (!lua_istable(L, -1)) {
                luaL_error(L, "inputs must be a table");
            }
            lua_rawgetfield(L, -1, declared.inputs[call.input].c_str());
        }
        if (!lua_isfunction(L, -1)) {
            luaL_error(L, "%s must be a function", function);
        }
        auto& result = *static_cast<CallResult*>(data);
        lua_pushvalue(L, game);
        if (call.input == kScriptDecide) {
            pushSubject(L, call);
            lua_call(L, 2, 1);
            if (!lua_isboolean(L, -1) && !lua_isnil(L, -1)) {
                luaL_error(L, "decide must return true or false, not a %s", luaL_typename(L, -1));
            }
            result.decision = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        } else {
            lua_call(L, 1, 0);
        }
        lua_rawgetfield(L, game, "state");
        readState(L, declared, result.values.data());
    }

    static void readStateDeclaration(lua_State* L, ScriptDeclarations& declarations) {
        if (!lua_istable(L, -1)) {
            luaL_error(L, "state must be a table of names and starting values");
        }
        const int table = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, table) != 0) {
            const char* name = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : nullptr;
            if (name == nullptr || !isValidStateName(name)) {
                luaL_error(L,
                           "state.%s: a name is letters, digits and '_', not starting with a "
                           "digit",
                           name != nullptr ? name : "?");
            }
            if (declarations.state.size() == kMaxScriptState) {
                luaL_error(L, "state may have at most %u values", kMaxScriptState);
            }
            ScriptVariable variable{name};
            if (lua_isboolean(L, -1)) {
                variable.flag = true;
                variable.initial = lua_toboolean(L, -1) ? 1.0 : 0.0;
            } else if (lua_type(L, -1) == LUA_TNUMBER && std::isfinite(lua_tonumber(L, -1))) {
                variable.initial = lua_tonumber(L, -1);
            } else {
                luaL_error(L, "state.%s must start as a number or true or false", name);
            }
            declarations.state.push_back(std::move(variable));
            lua_pop(L, 1);
        }
        std::sort(declarations.state.begin(), declarations.state.end(),
                  [](const ScriptVariable& a, const ScriptVariable& b) { return a.name < b.name; });
    }

    static void readEvents(lua_State* L, ScriptDeclarations& declarations) {
        if (!lua_istable(L, -1)) {
            luaL_error(L, "events must be a list of names");
        }
        const int table = lua_gettop(L);
        const int count = lua_objlen(L, table);
        int entries = 0;
        lua_pushnil(L);
        while (lua_next(L, table) != 0) {
            ++entries;
            lua_pop(L, 1);
        }
        if (entries != count) {
            luaL_error(L, "events must be a list of names");
        }
        if (count > static_cast<int>(kMaxScriptEvents)) {
            luaL_error(L, "events may have at most %u names", kMaxScriptEvents);
        }
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, table, i);
            const char* name = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
            if (name == nullptr || !isValidEntityName(name)) {
                luaL_error(L, "events[%d] must be a name: letters, digits, '_', '.' or '-'", i);
            }
            if (std::find(declarations.events.begin(), declarations.events.end(), name) !=
                declarations.events.end()) {
                luaL_error(L, "events has '%s' twice", name);
            }
            declarations.events.emplace_back(name);
            lua_pop(L, 1);
        }
    }

    static void readInputs(lua_State* L, ScriptDeclarations& declarations) {
        if (!lua_istable(L, -1)) {
            luaL_error(L, "inputs must be a table of functions");
        }
        const int table = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, table) != 0) {
            const char* name = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : nullptr;
            if (name == nullptr || !isValidEntityName(name)) {
                luaL_error(L, "inputs.%s: a name is letters, digits, '_', '.' or '-'",
                           name != nullptr ? name : "?");
            }
            if (!lua_isfunction(L, -1)) {
                luaL_error(L, "inputs.%s must be a function", name);
            }
            if (declarations.inputs.size() == kMaxScriptInputs) {
                luaL_error(L, "inputs may have at most %u functions", kMaxScriptInputs);
            }
            declarations.inputs.emplace_back(name);
            lua_pop(L, 1);
        }
        std::sort(declarations.inputs.begin(), declarations.inputs.end());
    }

    static void declareBody(lua_State* L, void* data) {
        ScriptDeclarations& declarations = *static_cast<ScriptDeclarations*>(data);
        const int script = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, script) != 0) {
            const char* key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?";
            const std::string_view field = key;
            if (field == "state") {
                readStateDeclaration(L, declarations);
            } else if (field == "events") {
                readEvents(L, declarations);
            } else if (field == "inputs") {
                readInputs(L, declarations);
            } else if (field == "start") {
                if (!lua_isfunction(L, -1)) {
                    luaL_error(L, "start must be a function");
                }
                declarations.start = true;
            } else if (field == "decide") {
                if (!lua_isfunction(L, -1)) {
                    luaL_error(L, "decide must be a function");
                }
                declarations.decide = true;
            } else {
                luaL_error(L,
                           "the table it returns has '%s'; it may have state, events, inputs "
                           "and start, or decide",
                           key);
            }
            lua_pop(L, 1);
        }
    }
};

ScriptRunner::ScriptRunner() = default;

ScriptRunner::~ScriptRunner() = default;

std::size_t ScriptRunner::memoryUsed() const { return m_arena ? m_arena->used() : 0; }

bool ScriptRunner::protect(std::string_view name, std::string_view bytecode,
                           void (*body)(lua_State*, void*), void* data) {
    m_effectCount = 0;
    m_printCount = 0;
    m_error = {};
    m_steps = 0;
    m_overBudget = false;
    if (!m_arena) {
        m_arena = std::make_unique<ScriptArena>(kScriptMemory);
    }
    // Nothing of the last call's state is closed: it lived in the arena, which starts again.
    m_arena->reset();
    lua_State* L = lua_newstate(&ScriptSandbox::allocate, this);
    if (L == nullptr) {
        fail("there is no memory for scripts");
        return false;
    }
    lua_callbacks(L)->userdata = this;
    lua_callbacks(L)->interrupt = &ScriptSandbox::interrupt;
    ProtectedRun run{name, bytecode, body, data};
    const int status = lua_cpcall(L, &ScriptSandbox::runProtected, &run);
    // Over budget, the call fails even if the script caught the error.
    if (status != LUA_OK && !m_overBudget) {
        const char* message = lua_tostring(L, -1);
        fail(message != nullptr ? message : "failed with an error that isn't text");
    }
    const bool finished = status == LUA_OK && !m_overBudget;
    if (!finished) {
        m_effectCount = 0;
    }
    return finished;
}

bool ScriptRunner::run(ScriptCall& call) {
    const ScriptDeclarations& declared = call.program->declarations();
    const bool known = call.input == kScriptStart    ? declared.start
                       : call.input == kScriptDecide ? declared.decide
                                                     : call.input < declared.inputs.size();
    if (call.state.size() != declared.state.size() || !known) {
        m_effectCount = 0;
        m_printCount = 0;
        assign(m_error, "the call doesn't match what the script declares");
        return false;
    }
    m_call = &call;
    m_random = call.random;
    ScriptSandbox::CallResult result;
    const bool finished =
        protect(call.program->name(), call.program->bytecode(), &ScriptSandbox::callBody, &result);
    m_call = nullptr;
    if (finished) {
        std::copy_n(result.values.begin(), call.state.size(), call.state.begin());
        call.random = m_random;
        m_decision = result.decision;
    }
    return finished;
}

bool ScriptRunner::declare(std::string_view name, std::string_view bytecode,
                           ScriptDeclarations& declarations) {
    ScriptDeclarations found;
    if (!protect(name, bytecode, &ScriptSandbox::declareBody, &found)) {
        return false;
    }
    declarations = std::move(found);
    return true;
}

void ScriptRunner::fail(std::string_view message) { assign(m_error, message); }

void ScriptRunner::addEffect(lua_State* L, const ScriptEffect& effect) {
    if (m_effectCount == kMaxScriptEffects) {
        luaL_error(L, "asked for more than %u things in one call", kMaxScriptEffects);
    }
    m_effects[m_effectCount++] = effect;
}

void ScriptRunner::print(std::string_view line) {
    if (m_printCount < kMaxScriptPrints) {
        assign(m_printed[m_printCount++], line);
    }
}

double ScriptRunner::nextRandom() {
    return static_cast<double>(randomBits(m_random++)) * (1.0 / 4294967296.0);
}

void ScriptRunner::countStep(lua_State* L) {
    if (++m_steps <= kScriptStepBudget) {
        return;
    }
    if (!m_overBudget) {
        m_overBudget = true;
        char message[64];
        std::snprintf(message, sizeof(message), "took more than %u steps", kScriptStepBudget);
        fail(message);
    }
    luaL_error(L, "took more than %u steps", kScriptStepBudget);
}

void* ScriptRunner::reallocate(void* block, std::size_t oldSize, std::size_t newSize) {
    if (newSize == 0) {
        m_arena->release(block, oldSize);
        return nullptr;
    }
    void* moved = m_arena->resize(block, oldSize, newSize);
    if (moved == nullptr && !m_overBudget) {
        m_overBudget = true;
        char message[64];
        std::snprintf(message, sizeof(message), "used more than %zu MB of memory",
                      kScriptMemory >> 20);
        fail(message);
    }
    return moved;
}

} // namespace ct
