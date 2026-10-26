#include "EnemyTick.h"
#include "LevelLogic.h"
#include "ScriptHost.h"

#include "croftech/core/Angle.h"
#include "croftech/core/Profile.h"
#include "croftech/script/ScriptProgram.h"
#include "croftech/script/ScriptRunner.h"
#include "croftech/sim/Health.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace ct {
namespace {

std::uint32_t delayTicks(double seconds) {
    return static_cast<std::uint32_t>(seconds * static_cast<double>(kTickRate) + 0.5);
}

double secondsOf(std::uint32_t ticks) {
    return static_cast<double>(ticks) / static_cast<double>(kTickRate);
}

ScriptValue number(std::string_view name, double value) {
    ScriptValue made;
    made.name = name;
    made.x = value;
    return made;
}

ScriptValue flag(std::string_view name, bool value) {
    ScriptValue made = number(name, value ? 1.0 : 0.0);
    made.kind = ScriptValueKind::Flag;
    return made;
}

ScriptValue text(std::string_view name, std::string_view value) {
    ScriptValue made = number(name, 0.0);
    made.kind = ScriptValueKind::Text;
    made.text = value;
    return made;
}

ScriptValue vector(std::string_view name, Vec3 value) {
    ScriptValue made = number(name, value.x);
    made.kind = ScriptValueKind::Vector;
    made.y = value.y;
    made.z = value.z;
    return made;
}

double degreesOf(std::uint32_t angle) {
    return static_cast<double>(angle % kAngleUnitsPerTurn) * 360.0 /
           static_cast<double>(kAngleUnitsPerTurn);
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
        case ScriptEffectKind::Send:
            sendFromScript(index, effect, activator);
            break;
        }
    }
}

BehaviourStatus LevelLogic::decide(EnemyTick& think, const BehaviourNode& node) {
    CT_PROFILE_ZONE();
    const ScriptProgram* program = behaviourScript(think.settings, node.script);
    if (program == nullptr || m_scripts == nullptr) {
        return BehaviourStatus::Failure;
    }
    viewTarget(think);
    const World& world = m_state.world;
    Enemy& enemy = think.enemy;
    const TickIndex tick = m_state.tick;
    std::string_view typeName;
    if (enemy.type < think.settings.enemies.size()) {
        typeName = think.settings.enemies[enemy.type].name;
    }

    const std::array subject{
        text("type", typeName),
        text("state", enemyStateName(enemyState(enemy))),
        number("state_time", secondsOf(tick - enemy.stateTick)),
        number("health", think.health.health),
        number("max_health", think.type.health),
        number("hurt_time",
               think.health.hurtTick == 0 ? -1.0 : secondsOf(tick - think.health.hurtTick)),
        vector("position", think.transform.position),
        number("yaw", degreesOf(think.transform.yaw)),
        vector("post", enemy.home),
        flag("ambush", (enemy.flags & kEnemyAmbush) != 0),
    };
    std::array<ScriptValue, 10> target{};
    std::size_t targetCount = 0;
    if (think.living) {
        const Health* health = world.healths.get(enemy.target);
        const PlayerMotion* motion = world.playerMotions.get(enemy.target);
        target = {
            vector("position", world.transforms.get(enemy.target)->position),
            vector("velocity", motion != nullptr ? motion->velocity : Vec3{}),
            number("health", health != nullptr ? health->health : 0),
            number("armour", health != nullptr ? health->armour : 0),
            number("distance", think.distance),
            flag("visible", think.visible),
            flag("in_reach", think.inReach),
            flag("in_range", think.inRange),
            vector("last_seen", enemy.lastSeen),
            number("seen_time", secondsOf(tick - enemy.seenTick)),
        };
        targetCount = target.size();
    }
    std::array<ScriptValue, 64> settings{};
    std::size_t settingCount = 0;
    const std::span<const EnemySetting> all = enemySettings();
    for (std::uint32_t i = 0; i < all.size() && settingCount < settings.size(); ++i) {
        if (all[i].wholeField != nullptr || all[i].numberField != nullptr) {
            settings[settingCount++] = number(all[i].key, enemySettingValue(think.type, i));
        }
    }

    ScriptCall call;
    call.program = program;
    call.input = kScriptDecide;
    call.random = enemy.random;
    call.time = secondsOf(tick);
    call.subject = subject;
    call.target = std::span(target).first(targetCount);
    call.settings = std::span(settings).first(settingCount);
    ScriptRunner& runner = m_scripts->runner;
    const bool finished = runner.run(call);
    for (const FixedText<kMaxScriptPrintLength>& line : runner.printed()) {
        if (!m_scripts->isRepeat(node.script, false, line.view())) {
            m_scripts->report(false, std::format("{}: {}", typeName, line.view()));
        }
    }
    if (!finished) {
        if (!m_scripts->isRepeat(node.script, true, runner.error())) {
            m_scripts->report(true, std::format("{} (behaviour {}): {}", typeName, think.tree.name,
                                                runner.error()));
        }
        return BehaviourStatus::Failure;
    }
    enemy.random = call.random;
    for (const ScriptEffect& effect : runner.effects()) {
        if (effect.kind == ScriptEffectKind::Send) {
            sendFromScript(think.wiring, effect, think.id, node.script);
        }
    }
    return runner.decision() ? BehaviourStatus::Success : BehaviourStatus::Failure;
}

void LevelLogic::sendFromScript(std::uint32_t source, const ScriptEffect& effect,
                                EntityId activator, std::string_view behaviourScript) {
    // A behaviour script's problem is told apart from its last by the names it sent.
    const auto isRepeat = [&] {
        if (behaviourScript.empty()) {
            return false;
        }
        const std::string_view to = effect.target.view();
        const std::string_view input = effect.targetInput.view();
        std::array<char, kMaxNameLength * 2 + 1> sent{};
        std::copy(to.begin(), to.end(), sent.data());
        sent[to.size()] = ' ';
        std::copy(input.begin(), input.end(), sent.data() + to.size() + 1);
        return m_scripts->isRepeat(behaviourScript, true,
                                   {sent.data(), to.size() + 1 + input.size()});
    };
    const std::span<const GameLevel::NamedEntity> targets =
        m_level.entitiesNamed(effect.target.view());
    if (targets.empty() && !isRepeat()) {
        m_scripts->report(true, std::format("{}: game:send: no entity is named '{}'",
                                            m_level.describe(source), effect.target.view()));
    }
    for (const GameLevel::NamedEntity& target : targets) {
        const std::optional<LogicInput> found =
            m_level.findInput(target.index, effect.targetInput.view());
        if (!found) {
            if (!isRepeat()) {
                m_scripts->report(true, std::format("{}: game:send: {} has no input '{}'",
                                                    m_level.describe(source),
                                                    m_level.describe(target.index),
                                                    effect.targetInput.view()));
            }
            continue;
        }
        sendDirect(source, target.index, *found, delayTicks(effect.delay), activator);
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
