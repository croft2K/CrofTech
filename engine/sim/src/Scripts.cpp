#include "LevelLogic.h"
#include "ScriptHost.h"

#include "croftech/core/Profile.h"
#include "croftech/script/ScriptProgram.h"
#include "croftech/script/ScriptRunner.h"

#include <format>

namespace ct {
namespace {

std::uint32_t delayTicks(double seconds) {
    return static_cast<std::uint32_t>(seconds * static_cast<double>(kTickRate) + 0.5);
}

} // namespace

void LevelLogic::runScript(std::uint32_t index, const GameLevel::Entity& entity, LogicInput input,
                           EntityId activator) {
    CT_PROFILE_ZONE();
    Script* script = m_state.world.scripts.get(entity.id);
    if (script == nullptr || entity.script == nullptr || m_scripts == nullptr) {
        return;
    }
    const ScriptDeclarations& declared = entity.script->declarations();
    ScriptCall call;
    call.program = entity.script;
    // States can come from untrusted saves, so the input is checked against the script.
    const auto number = static_cast<std::uint32_t>(input);
    if (input == kScriptStartInput) {
        if (!declared.start) {
            return;
        }
    } else if (number >= kFirstScriptSignal &&
               number - kFirstScriptSignal < declared.inputs.size()) {
        call.input = number - kFirstScriptSignal;
    } else {
        return;
    }
    call.state = std::span(script->values).first(declared.state.size());
    call.random = script->random;
    call.time = static_cast<double>(m_state.tick) / static_cast<double>(kTickRate);

    ScriptRunner& runner = m_scripts->runner;
    const bool finished = runner.run(call);
    for (const FixedText<kMaxScriptPrintLength>& line : runner.printed()) {
        m_scripts->report(false, std::format("{}: {}", m_level.describe(index), line.view()));
    }
    if (!finished) {
        m_scripts->report(true, std::format("{}: {}", m_level.describe(index), runner.error()));
        return;
    }
    script->random = call.random;
    ++script->calls;

    for (const ScriptEffect& effect : runner.effects()) {
        switch (effect.kind) {
        case ScriptEffectKind::Fire:
            fire(index, scriptEvent(effect.index), activator);
            break;
        case ScriptEffectKind::After:
            sendDirect(index, index, scriptInput(effect.index), delayTicks(effect.delay),
                       activator);
            break;
        case ScriptEffectKind::Send: {
            const std::span<const GameLevel::NamedEntity> targets =
                m_level.entitiesNamed(effect.target.view());
            if (targets.empty()) {
                m_scripts->report(true, std::format("{}: game:send: no entity is named '{}'",
                                                    m_level.describe(index), effect.target.view()));
            }
            for (const GameLevel::NamedEntity& target : targets) {
                const std::optional<LogicInput> found =
                    m_level.findInput(target.index, effect.targetInput.view());
                if (!found) {
                    m_scripts->report(true, std::format("{}: game:send: {} has no input '{}'",
                                                        m_level.describe(index),
                                                        m_level.describe(target.index),
                                                        effect.targetInput.view()));
                    continue;
                }
                sendDirect(index, target.index, *found, delayTicks(effect.delay), activator);
            }
            break;
        }
        }
    }
}

void LevelLogic::sendDirect(std::uint32_t source, std::uint32_t target, LogicInput input,
                            std::uint32_t delay, EntityId activator) {
    if (delay == 0) {
        queueInput(target, input, activator);
        return;
    }
    World& world = m_state.world;
    const EntityId id = world.entities.create();
    if (!id.isValid()) {
        return; // the world is full; the input is lost
    }
    world.pendingInputs.add(id,
                            {m_state.tick + delay, m_state.tick, m_sentOrder++, source,
                             kDirectInput, activator, target, static_cast<std::uint32_t>(input)});
}

} // namespace ct
