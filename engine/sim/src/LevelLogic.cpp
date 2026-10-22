#include "LevelLogic.h"

#include "croftech/core/Profile.h"
#include "croftech/level/EntityClasses.h"
#include "croftech/sim/Damage.h"
#include "croftech/sim/Firing.h"

#include <algorithm>
#include <cstddef>
#include <tuple>

namespace ct {

const GameLevel::Entity* LevelLogic::levelEntityOf(EntityId id, LogicClass logicClass,
                                                   std::uint32_t& index) const {
    const LevelEntityRef* ref = m_state.world.levelEntities.get(id);
    if (ref == nullptr) {
        return nullptr;
    }
    const GameLevel::Entity* entity = m_level.entity(ref->index);
    if (entity == nullptr || entity->logicClass != logicClass) {
        return nullptr;
    }
    index = ref->index;
    return entity;
}

void LevelLogic::deliverPendingInputs() {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    struct Due {
        std::uint32_t deliverTick;
        std::uint32_t sentTick;
        std::uint32_t sentOrder;
        EntityId id;
    };
    std::array<Due, kMaxEntities> due;
    std::size_t dueCount = 0;
    const std::span<const PendingInput> pending = world.pendingInputs.components();
    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (pending[i].deliverTick <= m_state.tick) {
            due[dueCount++] = {pending[i].deliverTick, pending[i].sentTick, pending[i].sentOrder,
                               world.pendingInputs.entities()[i]};
        }
    }
    std::sort(due.begin(), due.begin() + static_cast<std::ptrdiff_t>(dueCount),
              [](const Due& a, const Due& b) {
                  return std::tie(a.deliverTick, a.sentTick, a.sentOrder, a.id.index) <
                         std::tie(b.deliverTick, b.sentTick, b.sentOrder, b.id.index);
              });
    for (std::size_t i = 0; i < dueCount; ++i) {
        const PendingInput input = *world.pendingInputs.get(due[i].id);
        world.destroy(due[i].id);
        const GameLevel::Entity* source = m_level.entity(input.source);
        if (source != nullptr && input.output < source->outputs.size()) {
            queue(source->outputs[input.output], input.activator);
        }
    }
    deliverQueued();
}

void LevelLogic::runTimers() {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    for (std::size_t i = 0; i < world.timers.size(); ++i) {
        Timer& timer = world.timers.components()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity =
            levelEntityOf(world.timers.entities()[i], LogicClass::Timer, index);
        if (entity == nullptr || (timer.flags & kLogicEnabled) == 0 ||
            timer.nextTick > m_state.tick) {
            continue;
        }
        if (entity->repeat) {
            timer.nextTick = m_state.tick + entity->intervalTicks;
        } else {
            timer.flags &= ~kLogicEnabled;
        }
        fire(index, LogicEvent::OnTimer, {});
    }
    deliverQueued();
}

void LevelLogic::updateTriggers(const MovementSettings& movement) {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    std::array<EntityId, kMaxPlayers> players{};
    std::array<Vec3, kMaxPlayers> positions{};
    std::uint32_t present = 0;
    const std::span<const PlayerControl> controls = world.playerControls.components();
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const std::uint32_t slot = controls[i].inputSlot;
        const EntityId id = world.playerControls.entities()[i];
        const Transform* transform = world.transforms.get(id);
        const Health* health = world.healths.get(id);
        if (health != nullptr && isDead(*health)) {
            continue; // the dead set nothing off
        }
        if (slot < kMaxPlayers && transform != nullptr && (present & (1u << slot)) == 0) {
            present |= 1u << slot;
            players[slot] = id;
            positions[slot] = transform->position;
        }
    }

    for (std::size_t i = 0; i < world.triggers.size(); ++i) {
        Trigger& trigger = world.triggers.components()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity =
            levelEntityOf(world.triggers.entities()[i], LogicClass::Trigger, index);
        if (entity == nullptr) {
            continue;
        }
        std::uint32_t inside = 0;
        if ((trigger.flags & kLogicEnabled) != 0 && entity->model != nullptr) {
            for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
                if ((present & (1u << slot)) != 0 &&
                    entity->model->overlapsBox(positions[slot], movement.halfExtents)) {
                    inside |= 1u << slot;
                }
            }
        }
        const std::uint32_t left = trigger.playersInside & ~inside;
        const std::uint32_t entered = inside & ~trigger.playersInside;
        trigger.playersInside = inside;
        for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
            if ((left & (1u << slot)) != 0) {
                fire(index, LogicEvent::OnExit, players[slot]);
            }
        }
        for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
            if ((entered & (1u << slot)) == 0) {
                continue;
            }
            fire(index, LogicEvent::OnEnter, players[slot]);
            if (entity->once) {
                trigger.flags &= ~kLogicEnabled;
                trigger.playersInside = 0;
                break;
            }
        }
    }

    for (std::size_t i = 0; i < world.checkpoints.size(); ++i) {
        Checkpoint& checkpoint = world.checkpoints.components()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity =
            levelEntityOf(world.checkpoints.entities()[i], LogicClass::Checkpoint, index);
        if (entity == nullptr || entity->model == nullptr ||
            (checkpoint.flags & kLogicEnabled) == 0 ||
            (checkpoint.flags & kCheckpointReached) != 0) {
            continue;
        }
        for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
            if ((present & (1u << slot)) != 0 &&
                entity->model->overlapsBox(positions[slot], movement.halfExtents)) {
                checkpoint.flags |= kCheckpointReached;
                checkpoint.reachedTick = m_state.tick;
                fire(index, LogicEvent::OnReached, players[slot]);
                break;
            }
        }
    }
    deliverQueued();
}

void LevelLogic::runHurtVolumes(const GameSettings& settings) {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    for (std::size_t i = 0; i < world.hurts.size(); ++i) {
        std::uint32_t index = 0;
        const GameLevel::Entity* entity =
            levelEntityOf(world.hurts.entities()[i], LogicClass::Hurt, index);
        if (entity == nullptr || entity->model == nullptr ||
            (world.hurts.components()[i].flags & kLogicEnabled) == 0) {
            continue;
        }
        for (std::size_t j = 0; j < world.healths.size(); ++j) {
            const EntityId target = world.healths.entities()[j];
            Health& health = world.healths.components()[j];
            const Transform* transform = world.transforms.get(target);
            if (transform == nullptr || isDead(health) || health.hazardTick > m_state.tick ||
                !entity->model->overlapsBox(transform->position, settings.movement.halfExtents)) {
                continue;
            }
            health.hazardTick = m_state.tick + entity->intervalTicks;
            Damage damage;
            damage.amount = entity->damage;
            damage.kind = DamageKind::Hazard;
            applyDamage(m_state, settings.rules, target, damage);
        }
    }
}

void LevelLogic::runPickups(const GameSettings& settings) {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    const Vec3 half = settings.movement.halfExtents;
    for (std::size_t i = 0; i < world.pickups.size(); ++i) {
        Pickup& pickup = world.pickups.components()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity =
            levelEntityOf(world.pickups.entities()[i], LogicClass::Pickup, index);
        if (entity == nullptr) {
            continue;
        }
        if ((pickup.flags & kPickupTaken) != 0 && pickup.respawnTick != 0 &&
            m_state.tick >= pickup.respawnTick) {
            pickup.flags &= ~kPickupTaken;
            pickup.respawnTick = 0;
        }
        if (!isAvailable(pickup)) {
            continue;
        }
        const Aabb box{entity->position - kPickupHalfExtents,
                       entity->position + kPickupHalfExtents};
        const std::span<const EntityId> players = world.playerControls.entities();
        for (const EntityId player : players) {
            const Transform* transform = world.transforms.get(player);
            const Health* health = world.healths.get(player);
            if (transform == nullptr || (health != nullptr && isDead(*health)) ||
                !overlaps({transform->position - half, transform->position + half}, box) ||
                !give(*entity, settings, player)) {
                continue;
            }
            pickup.flags |= kPickupTaken;
            pickup.takenBy = player;
            pickup.respawnTick = entity->respawnTicks < 0
                                     ? 0u
                                     : m_state.tick + static_cast<TickIndex>(entity->respawnTicks);
            fire(index, LogicEvent::OnPickup, player);
            break;
        }
    }
    deliverQueued();
}

bool LevelLogic::give(const GameLevel::Entity& pickup, const GameSettings& settings,
                      EntityId player) {
    World& world = m_state.world;
    Health* health = world.healths.get(player);
    Weapons* weapons = world.weapons.get(player);
    // Tops a value up by the amount, to the cap; false if it is there already.
    const auto topUp = [](std::int32_t& value, std::int32_t amount, std::int32_t cap) {
        if (value >= cap) {
            return false;
        }
        value = static_cast<std::int32_t>(
            std::min<std::int64_t>(static_cast<std::int64_t>(value) + amount, cap));
        return true;
    };
    const std::int32_t limit = pickup.limit;
    switch (pickup.item) {
    case PickupItem::Health:
        return health != nullptr &&
               topUp(health->health, pickup.amount > 0 ? pickup.amount : kUsualPickupHealth,
                     limit > 0 ? limit : settings.rules.maxHealth);
    case PickupItem::Armour:
        return health != nullptr &&
               topUp(health->armour, pickup.amount > 0 ? pickup.amount : kUsualPickupArmour,
                     limit > 0 ? limit : settings.rules.maxArmour);
    case PickupItem::Ammo:
    case PickupItem::Weapon: {
        if (weapons == nullptr || pickup.weapon >= settings.weapons.size()) {
            return false;
        }
        const std::int32_t ammo =
            pickup.amount > 0 ? pickup.amount : settings.weapons[pickup.weapon].pickupAmmo;
        return pickup.item == PickupItem::Ammo
                   ? giveAmmo(*weapons, settings, pickup.weapon, ammo)
                   : giveWeapon(*weapons, settings, pickup.weapon, ammo, m_state.tick);
    }
    }
    return false;
}

void LevelLogic::fire(std::uint32_t source, LogicEvent event, EntityId activator) {
    const GameLevel::Entity* entity = m_level.entity(source);
    if (entity == nullptr) {
        return;
    }
    for (std::uint32_t i = 0; i < entity->outputs.size(); ++i) {
        if (entity->outputs[i].event == event) {
            send(source, i, activator);
        }
    }
}

void LevelLogic::send(std::uint32_t source, std::uint32_t output, EntityId activator) {
    const GameLevel::Output& wire = m_level.entity(source)->outputs[output];
    if (wire.delayTicks == 0) {
        queue(wire, activator);
        return;
    }
    World& world = m_state.world;
    const EntityId id = world.entities.create();
    if (!id.isValid()) {
        return; // the world is full; the input is lost
    }
    world.pendingInputs.add(id, {m_state.tick + wire.delayTicks, m_state.tick, m_sentOrder++,
                                 source, output, activator});
}

void LevelLogic::queue(const GameLevel::Output& output, EntityId activator) {
    for (const GameLevel::Target& target : output.targets) {
        if (m_queueCount == m_queue.size()) {
            return;
        }
        m_queue[(m_queueHead + m_queueCount++) % m_queue.size()] = {target.entity, target.input,
                                                                    activator};
    }
}

void LevelLogic::deliverQueued() {
    while (m_queueCount > 0 && m_delivered < kMaxInputDeliveriesPerTick) {
        const Queued input = m_queue[m_queueHead];
        m_queueHead = (m_queueHead + 1) % m_queue.size();
        --m_queueCount;
        ++m_delivered;
        apply(input);
    }
    m_queueCount = 0;
}

void LevelLogic::apply(const Queued& input) {
    const GameLevel::Entity* entity = m_level.entity(input.target);
    if (entity == nullptr) {
        return;
    }
    World& world = m_state.world;
    const EntityId id = entity->id;
    switch (entity->logicClass) {
    case LogicClass::Trigger:
        if (Trigger* trigger = world.triggers.get(id)) {
            const bool enable =
                input.input == LogicInput::Enable ||
                (input.input == LogicInput::Toggle && (trigger->flags & kLogicEnabled) == 0);
            if (enable) {
                trigger->flags |= kLogicEnabled;
            } else if (input.input == LogicInput::Disable || input.input == LogicInput::Toggle) {
                // Players still inside when it is switched back on go in again.
                trigger->flags &= ~kLogicEnabled;
                trigger->playersInside = 0;
            }
        }
        return;
    case LogicClass::Hurt:
        if (Hurt* hurt = world.hurts.get(id)) {
            const bool enable =
                input.input == LogicInput::Enable ||
                (input.input == LogicInput::Toggle && (hurt->flags & kLogicEnabled) == 0);
            if (enable) {
                hurt->flags |= kLogicEnabled;
            } else if (input.input == LogicInput::Disable || input.input == LogicInput::Toggle) {
                hurt->flags &= ~kLogicEnabled;
            }
        }
        return;
    case LogicClass::Checkpoint:
        if (Checkpoint* checkpoint = world.checkpoints.get(id)) {
            const bool enable =
                input.input == LogicInput::Enable ||
                (input.input == LogicInput::Toggle && (checkpoint->flags & kLogicEnabled) == 0);
            if (enable) {
                checkpoint->flags |= kLogicEnabled;
            } else if (input.input == LogicInput::Disable || input.input == LogicInput::Toggle) {
                checkpoint->flags &= ~kLogicEnabled;
            }
        }
        return;
    case LogicClass::Relay:
        if (Relay* relay = world.relays.get(id)) {
            if (input.input == LogicInput::Enable) {
                relay->flags |= kLogicEnabled;
            } else if (input.input == LogicInput::Disable) {
                relay->flags &= ~kLogicEnabled;
            } else if (input.input == LogicInput::Trigger && (relay->flags & kLogicEnabled) != 0) {
                fire(input.target, LogicEvent::OnTrigger, input.activator);
            }
        }
        return;
    case LogicClass::Counter:
        if (Counter* counter = world.counters.get(id)) {
            if (input.input == LogicInput::Reset) {
                counter->value = 0;
            } else if (input.input == LogicInput::Add && ++counter->value >= entity->count) {
                counter->value = 0;
                fire(input.target, LogicEvent::OnReached, input.activator);
            }
        }
        return;
    case LogicClass::Timer:
        if (Timer* timer = world.timers.get(id)) {
            const bool start =
                input.input == LogicInput::Start ||
                (input.input == LogicInput::Toggle && (timer->flags & kLogicEnabled) == 0);
            if (start) {
                timer->flags |= kLogicEnabled;
                timer->nextTick = m_state.tick + entity->intervalTicks;
            } else if (input.input == LogicInput::Stop || input.input == LogicInput::Toggle) {
                timer->flags &= ~kLogicEnabled;
            }
        }
        return;
    case LogicClass::Mover:
        if (Mover* mover = world.movers.get(id)) {
            applyMoverInput(input.target, *entity, *mover, input.input, input.activator);
        }
        return;
    case LogicClass::Pickup:
        if (Pickup* pickup = world.pickups.get(id)) {
            const bool enable =
                input.input == LogicInput::Enable ||
                (input.input == LogicInput::Toggle && (pickup->flags & kLogicEnabled) == 0);
            if (enable) {
                pickup->flags |= kLogicEnabled;
            } else if (input.input == LogicInput::Disable || input.input == LogicInput::Toggle) {
                pickup->flags &= ~kLogicEnabled;
            }
        }
        return;
    case LogicClass::None:
        return;
    }
}

} // namespace ct
