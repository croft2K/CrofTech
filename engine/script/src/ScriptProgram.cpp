#include "croftech/script/ScriptProgram.h"

#include "ScriptSandbox.h"

#include "croftech/script/ScriptRunner.h"

#include <Luau/Ast.h>
#include <Luau/BytecodeBuilder.h>
#include <Luau/Compiler.h>
#include <Luau/Parser.h>
#include <algorithm>
#include <format>

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

std::string located(std::string_view name, const Luau::Location& location,
                    std::string_view message) {
    return std::format("{}:{}: {}", name, location.begin.line + 1, message);
}

} // namespace

bool isValidScriptName(std::string_view name) {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::optional<ScriptProgram> ScriptProgram::load(std::string_view name, std::string_view source,
                                                 std::string& error) {
    if (!isValidScriptName(name)) {
        error =
            std::format("'{}' isn't a script name: lowercase letters, digits, '-' or '_'", name);
        return std::nullopt;
    }
    if (source.size() > kMaxScriptBytes) {
        error = std::format("{} is over {} KB", name, kMaxScriptBytes / 1024);
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
        error = std::string(runner.error());
        return std::nullopt;
    }
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
