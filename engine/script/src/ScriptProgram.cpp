#include "croftech/script/ScriptProgram.h"

#include "ScriptSandbox.h"

#include "croftech/script/ScriptRunner.h"

#include <Luau/Ast.h>
#include <Luau/BytecodeBuilder.h>
#include <Luau/Compiler.h>
#include <Luau/Parser.h>
#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

namespace ct {
namespace {

// Finds the first thing in a script that the sandbox doesn't allow.
class SandboxCheck : public Luau::AstVisitor {
public:
    std::optional<Luau::Location> where;
    std::string problem;

    bool visit(Luau::AstExprBinary* node) override {
        if (node->op == Luau::AstExprBinary::Pow) {
            report(node->location, "the ^ operator isn't available to scripts, as pow gives "
                                   "different results on different platforms; multiply instead");
        }
        return true;
    }

    bool visit(Luau::AstStatCompoundAssign* node) override {
        if (node->op == Luau::AstExprBinary::Pow) {
            report(node->location, "the ^= operator isn't available to scripts, as pow gives "
                                   "different results on different platforms; multiply instead");
        }
        return true;
    }

    bool visit(Luau::AstExprIndexName* node) override {
        const Luau::AstExprGlobal* library = node->expr->as<Luau::AstExprGlobal>();
        if (library == nullptr || std::string_view(library->name.value) != "math") {
            return true;
        }
        const std::string_view name = node->index.value;
        const std::span<const std::string_view> names = scriptMathNames();
        if (std::find(names.begin(), names.end(), name) != names.end()) {
            return true;
        }
        if (name == "random") {
            report(node->location, "math.random isn't available to scripts; use game:random()");
        } else {
            report(node->location,
                   std::format("math.{} isn't available to scripts, as it gives different "
                               "results on different platforms",
                               name));
        }
        return true;
    }

    bool visit(Luau::AstExprGlobal* node) override {
        const std::string_view name = node->name.value;
        const std::span<const std::string_view> missing = scriptMissingGlobals();
        if (std::find(missing.begin(), missing.end(), name) != missing.end()) {
            report(node->location, std::format("{} isn't available to scripts", name));
        }
        return true;
    }

private:
    void report(const Luau::Location& location, std::string text) {
        if (!where) {
            where = location;
            problem = std::move(text);
        }
    }
};

// Finds the calls game:fire("event"), game:after(seconds, "input") and game:send("name", "input").
class CallFinder : public Luau::AstVisitor {
public:
    std::vector<ScriptCallSite> sites;

    bool visit(Luau::AstExprCall* node) override {
        const Luau::AstExprIndexName* function = node->func->as<Luau::AstExprIndexName>();
        if (function == nullptr || !namedGame(function->expr)) {
            return true;
        }
        const std::string_view method = function->index.value;
        const auto text = [&](std::size_t index) -> std::optional<std::string> {
            if (index >= node->args.size) {
                return std::nullopt;
            }
            const Luau::AstExprConstantString* string =
                node->args.data[index]->as<Luau::AstExprConstantString>();
            if (string == nullptr) {
                return std::nullopt;
            }
            return std::string(string->value.data, string->value.size);
        };
        ScriptCallSite site;
        site.line = node->location.begin.line + 1;
        if (method == "fire" && text(0)) {
            site.kind = ScriptCallKind::Fire;
            site.name = *text(0);
        } else if (method == "after" && text(1)) {
            site.kind = ScriptCallKind::After;
            site.name = *text(1);
        } else if (method == "send" && text(0) && text(1)) {
            site.kind = ScriptCallKind::Send;
            site.name = *text(0);
            site.input = *text(1);
        } else {
            return true;
        }
        sites.push_back(std::move(site));
        return true;
    }

private:
    static bool namedGame(Luau::AstExpr* expr) {
        if (const Luau::AstExprLocal* local = expr->as<Luau::AstExprLocal>()) {
            return std::string_view(local->local->name.value) == "game";
        }
        if (const Luau::AstExprGlobal* global = expr->as<Luau::AstExprGlobal>()) {
            return std::string_view(global->name.value) == "game";
        }
        return false;
    }
};

ScriptError located(std::string_view name, const Luau::Location& location,
                    std::string_view message) {
    return {std::format("{}:{}: {}", name, location.begin.line + 1, message),
            location.begin.line + 1};
}

} // namespace

bool isValidScriptName(std::string_view name) {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::optional<ScriptProgram> ScriptProgram::load(std::string_view name, std::string_view source,
                                                 std::string& error) {
    ScriptError failure;
    std::optional<ScriptProgram> program = load(name, source, failure);
    if (!program) {
        error = std::move(failure.text);
    }
    return program;
}

std::optional<ScriptProgram> ScriptProgram::load(std::string_view name, std::string_view source,
                                                 ScriptError& error) {
    error = {};
    if (!isValidScriptName(name)) {
        error.text =
            std::format("'{}' isn't a script name: lowercase letters, digits, '-' or '_'", name);
        return std::nullopt;
    }
    if (source.size() > kMaxScriptBytes) {
        error.text = std::format("{} is over {} KB", name, kMaxScriptBytes / 1024);
        return std::nullopt;
    }

    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    const Luau::ParseResult parsed =
        Luau::Parser::parse(source.data(), source.size(), names, allocator);
    if (!parsed.errors.empty()) {
        error =
            located(name, parsed.errors.front().getLocation(), parsed.errors.front().getMessage());
        return std::nullopt;
    }
    // These raise the optimisation level, which folds calls of builtins such as math.sin with the
    // compiling machine's maths.
    for (const Luau::HotComment& comment : parsed.hotcomments) {
        if (comment.header &&
            (comment.content.starts_with("optimize") || comment.content.starts_with("native"))) {
            error = located(name, comment.location,
                            std::format("--!{} isn't allowed in scripts", comment.content));
            return std::nullopt;
        }
    }
    SandboxCheck check;
    parsed.root->visit(&check);
    if (check.where) {
        error = located(name, *check.where, check.problem);
        return std::nullopt;
    }
    CallFinder calls;
    parsed.root->visit(&calls);

    ScriptProgram program;
    program.m_name = std::string(name);
    try {
        Luau::BytecodeBuilder bytecode;
        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 1;
        Luau::compileOrThrow(bytecode, parsed, names, options);
        program.m_bytecode = bytecode.getBytecode();
    } catch (const Luau::CompileError& compileError) {
        error = located(name, compileError.getLocation(), compileError.what());
        return std::nullopt;
    }

    ScriptRunner runner;
    if (!runner.declare(program.m_name, program.m_bytecode, program.m_declarations)) {
        error.text = std::string(runner.error());
        // Errors raised by the script itself say where: "name:line: message".
        const std::string prefix = program.m_name + ":";
        const std::size_t colon = error.text.find(':', prefix.size());
        if (error.text.starts_with(prefix) && colon != std::string::npos) {
            const std::string_view digits =
                std::string_view(error.text).substr(prefix.size(), colon - prefix.size());
            std::uint32_t line = 0;
            if (std::from_chars(digits.data(), digits.data() + digits.size(), line).ec ==
                std::errc{}) {
                error.line = line;
            }
        }
        return std::nullopt;
    }
    for (const ScriptCallSite& site : calls.sites) {
        const bool fire = site.kind == ScriptCallKind::Fire;
        const bool after = site.kind == ScriptCallKind::After;
        if ((fire && !program.findEvent(site.name)) || (after && !program.findInput(site.name))) {
            error.line = site.line;
            error.text = std::format("{}:{}: game:{}: no {} '{}' (declare it in {})", name,
                                     site.line, fire ? "fire" : "after", fire ? "event" : "input",
                                     site.name, fire ? "events" : "inputs");
            return std::nullopt;
        }
    }
    program.m_callSites = std::move(calls.sites);
    return program;
}

std::optional<std::uint32_t> ScriptProgram::findInput(std::string_view input) const {
    const std::vector<std::string>& inputs = m_declarations.inputs;
    const auto it = std::lower_bound(inputs.begin(), inputs.end(), input);
    if (it == inputs.end() || *it != input) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(it - inputs.begin());
}

std::optional<std::uint32_t> ScriptProgram::findEvent(std::string_view event) const {
    const std::vector<std::string>& events = m_declarations.events;
    const auto it = std::find(events.begin(), events.end(), event);
    if (it == events.end()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(it - events.begin());
}

} // namespace ct
