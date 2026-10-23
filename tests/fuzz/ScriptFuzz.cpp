// Fuzzes loading and running scripts, which may come with a level downloaded from a server. A
// script that loads keeps to the limits on what it declares, and each of its functions runs once:
// a call either finishes with every state value of its declared kind, or fails having asked for
// nothing.

#include "croftech/script/ScriptProgram.h"
#include "croftech/script/ScriptRunner.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static ct::ScriptRunner runner;
    std::string error;
    const std::optional<ct::ScriptProgram> program = ct::ScriptProgram::load(
        "fuzz", std::string_view(reinterpret_cast<const char*>(data), size), error);
    if (!program) {
        return 0;
    }
    const ct::ScriptDeclarations& declared = program->declarations();
    if (declared.state.size() > ct::kMaxScriptState ||
        declared.events.size() > ct::kMaxScriptEvents ||
        declared.inputs.size() > ct::kMaxScriptInputs) {
        std::abort();
    }
    std::array<double, ct::kMaxScriptState> values{};
    for (std::size_t i = 0; i < declared.state.size(); ++i) {
        values[i] = declared.state[i].initial;
    }
    for (std::uint32_t input = 0; input <= declared.inputs.size(); ++input) {
        ct::ScriptCall call;
        call.program = &*program;
        call.input = input < declared.inputs.size() ? input : ct::kScriptStart;
        if (call.input == ct::kScriptStart && !declared.start) {
            continue;
        }
        call.state = std::span(values).first(declared.state.size());
        if (!runner.run(call)) {
            if (!runner.effects().empty()) {
                std::abort();
            }
            continue;
        }
        for (std::size_t i = 0; i < declared.state.size(); ++i) {
            if (!std::isfinite(values[i]) ||
                (declared.state[i].flag && values[i] != 0.0 && values[i] != 1.0)) {
                std::abort();
            }
        }
        for (const ct::ScriptEffect& effect : runner.effects()) {
            const bool fire = effect.kind == ct::ScriptEffectKind::Fire;
            const bool after = effect.kind == ct::ScriptEffectKind::After;
            if ((fire && effect.index >= declared.events.size()) ||
                (after && effect.index >= declared.inputs.size()) ||
                !(effect.delay >= 0.0 && effect.delay <= ct::kMaxOutputDelay)) {
                std::abort();
            }
        }
    }
    return 0;
}
