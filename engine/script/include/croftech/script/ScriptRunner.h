#pragma once

#include "croftech/level/Level.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

struct lua_State;

namespace ct {

class ScriptArena;
class ScriptProgram;
struct ScriptDeclarations;

// What one call may use. Steps are Luau's interrupt points: loop iterations, calls and returns,
// and the steps of string pattern matching. A call that goes over either fails.
inline constexpr std::uint32_t kScriptStepBudget = 200'000;
inline constexpr std::size_t kScriptMemory = std::size_t{4} << 20;
inline constexpr std::uint32_t kMaxScriptEffects = 64;
inline constexpr std::uint32_t kMaxScriptPrints = 8;
inline constexpr std::size_t kMaxScriptPrintLength = 200;

// ScriptCall::input for the script's start function rather than an input.
inline constexpr std::uint32_t kScriptStart = 0xFFFFFFFFu;

// One call of a script. The state and the random counter go in, and come out changed if the call
// finishes.
struct ScriptCall {
    const ScriptProgram* program = nullptr;
    std::uint32_t input = kScriptStart; // index into the declared inputs
    std::span<double> state;            // a value per declared variable, in declared order
    std::uint32_t random = 0;           // counter for game:random (core/Random.h)
    double time = 0.0;                  // seconds since play began, as game.time
};

enum class ScriptEffectKind : std::uint8_t {
    Fire,  // one of its events, through its wiring
    After, // one of its own inputs, later
    Send,  // an input to every entity with a name, now or later
};

// Text up to a fixed length, so a call fills no heap memory.
template <std::size_t N>
struct FixedText {
    std::array<char, N> characters{};
    std::uint32_t length = 0;

    std::string_view view() const { return {characters.data(), length}; }
};

// Something a call asked for. The caller does it once the call has finished, so a call that
// fails does nothing.
struct ScriptEffect {
    ScriptEffectKind kind = ScriptEffectKind::Fire;
    std::uint32_t index = 0; // Fire: the event; After: the input
    double delay = 0.0;      // seconds; After and Send
    // Send: as the script gave them; whether an entity has the name and accepts the input is for
    // the caller to find out.
    FixedText<kMaxNameLength> target;
    FixedText<kMaxNameLength> targetInput;
};

// Runs script calls in a sandbox: a fresh Luau state in a fixed block of memory for each call
// (ScriptArena), with no I/O and only the parts of the standard library that give the same results
// everywhere, held to kScriptStepBudget steps and kScriptMemory bytes. Each call starts from the
// same place, so the same call gives the same result on every machine.
//
// The game's calls get a `game` table: game.state (the declared values), game.time,
// game:fire(event), game:after(seconds, input), game:send(name, input [, seconds]) and
// game:random() (in [0, 1)) or game:random(m, n) (a whole number from m to n). print writes
// lines that the caller can log.
//
// The memory is taken on the first call. Not thread-safe; a runner serves one thread.
class ScriptRunner {
public:
    ScriptRunner();
    ~ScriptRunner();
    ScriptRunner(const ScriptRunner&) = delete;
    ScriptRunner& operator=(const ScriptRunner&) = delete;

    // True if the call finished: then the call's state and counter hold their new values and
    // effects() lists what it asked for, in order. False if it failed, with error() saying why;
    // then the call is unchanged and there are no effects.
    bool run(ScriptCall& call);
    // Runs a compiled chunk on its own and reads the table it returns, for ScriptProgram::load.
    bool declare(std::string_view name, std::string_view bytecode,
                 ScriptDeclarations& declarations);

    std::span<const ScriptEffect> effects() const { return {m_effects.data(), m_effectCount}; }
    // Lines printed by the last run, whether it finished or not.
    std::span<const FixedText<kMaxScriptPrintLength>> printed() const {
        return {m_printed.data(), m_printCount};
    }
    std::string_view error() const { return m_error.view(); }
    // Of the last run: steps taken, and how far into its memory it reached.
    std::uint32_t steps() const { return m_steps; }
    std::size_t memoryUsed() const;

private:
    // The functions scripts call, which find their runner through the Luau state.
    friend class ScriptSandbox;

    // Runs the chunk in a fresh state, then body(L, data) with the table it returned on top of
    // the stack. False, with the error, if anything failed or went over budget.
    bool protect(std::string_view name, std::string_view bytecode, void (*body)(lua_State*, void*),
                 void* data);
    void fail(std::string_view message);
    void addEffect(lua_State* L, const ScriptEffect& effect);
    void print(std::string_view line);
    double nextRandom();
    void countStep(lua_State* L);
    void* reallocate(void* block, std::size_t oldSize, std::size_t newSize);

    std::unique_ptr<ScriptArena> m_arena;
    const ScriptCall* m_call = nullptr;
    std::uint32_t m_random = 0;
    std::array<ScriptEffect, kMaxScriptEffects> m_effects{};
    std::uint32_t m_effectCount = 0;
    std::array<FixedText<kMaxScriptPrintLength>, kMaxScriptPrints> m_printed{};
    std::uint32_t m_printCount = 0;
    FixedText<256> m_error;
    std::uint32_t m_steps = 0;
    bool m_overBudget = false; // steps or memory: the call fails even if the script caught it
};

} // namespace ct
