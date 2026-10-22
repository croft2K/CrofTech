// Movers: the LevelLogic phases and inputs that move doors, lifts and platforms.

#include "LevelLogic.h"

#include "croftech/core/Profile.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Enemies.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace ct {
namespace {

// A player this close to a door counts as walking into it; collision keeps players about a
// millimetre off surfaces, so they never quite touch.
constexpr float kTouchMargin = 0.02f;
// A pushed player is stuck if this far inside something solid. Rounding leaves players a hair
// closer to surfaces than collision keeps them, which mustn't count.
constexpr float kStuckInset = 1e-4f;
// Players one mover can push or carry at once; more and it is treated as blocked.
constexpr std::size_t kMaxPushed = 64;

MoverPhase phaseOf(const Mover& mover) { return static_cast<MoverPhase>(mover.phase); }

bool headingToStart(const Mover& mover) {
    return phaseOf(mover) == MoverPhase::AtStart || phaseOf(mover) == MoverPhase::ToStart;
}

} // namespace

void LevelLogic::applyMoverInput(std::uint32_t index, const GameLevel::Entity& entity, Mover& mover,
                                 LogicInput input, EntityId activator) {
    if (input == LogicInput::Stop) {
        mover.flags |= kMoverStopped;
        return;
    }
    mover.flags &= ~kMoverStopped;
    switch (input) {
    case LogicInput::MoveToEnd:
        if (phaseOf(mover) == MoverPhase::AtEnd) {
            extendWait(entity, mover);
        } else {
            setOff(index, mover, true, activator);
        }
        return;
    case LogicInput::MoveToStart:
        setOff(index, mover, false, activator);
        return;
    case LogicInput::Toggle:
        setOff(index, mover, headingToStart(mover), activator);
        return;
    default:
        return; // resume only clears the stop
    }
}

void LevelLogic::setOff(std::uint32_t index, Mover& mover, bool toEnd, EntityId activator) {
    const MoverPhase phase = phaseOf(mover);
    const MoverPhase moving = toEnd ? MoverPhase::ToEnd : MoverPhase::ToStart;
    const MoverPhase there = toEnd ? MoverPhase::AtEnd : MoverPhase::AtStart;
    if (phase == moving || phase == there) {
        return;
    }
    mover.phase = static_cast<std::uint32_t>(moving);
    mover.departTick = 0;
    if (phase == MoverPhase::AtStart) {
        fire(index, LogicEvent::OnLeaveStart, activator);
    } else if (phase == MoverPhase::AtEnd) {
        fire(index, LogicEvent::OnLeaveEnd, activator);
    }
}

void LevelLogic::arrive(std::uint32_t index, const GameLevel::Entity& entity, Mover& mover,
                        bool atEnd) {
    mover.phase = static_cast<std::uint32_t>(atEnd ? MoverPhase::AtEnd : MoverPhase::AtStart);
    mover.departTick = departureTick(entity, atEnd);
    fire(index, atEnd ? LogicEvent::OnReachedEnd : LogicEvent::OnReachedStart, {});
}

void LevelLogic::extendWait(const GameLevel::Entity& entity, Mover& mover) const {
    if (mover.departTick != 0) {
        mover.departTick = departureTick(entity, true);
    }
}

// When a mover that has just arrived sets off again by itself: from the end after its wait
// unless that is -1, and from the start only if it loops.
std::uint32_t LevelLogic::departureTick(const GameLevel::Entity& entity, bool atEnd) const {
    if (entity.waitTicks < 0 || (!atEnd && !entity.loop)) {
        return 0;
    }
    return m_state.tick + static_cast<std::uint32_t>(entity.waitTicks);
}

void LevelLogic::runMovers(const GameSettings& settings) {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    for (std::size_t i = 0; i < world.movers.size(); ++i) {
        const EntityId id = world.movers.entities()[i];
        Mover& mover = world.movers.components()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity = levelEntityOf(id, LogicClass::Mover, index);
        if (entity == nullptr || (mover.flags & kMoverStopped) != 0) {
            continue;
        }
        const MoverPhase phase = phaseOf(mover);
        if ((phase == MoverPhase::AtStart || phase == MoverPhase::AtEnd) && mover.departTick != 0 &&
            m_state.tick >= mover.departTick) {
            setOff(index, mover, phase == MoverPhase::AtStart, {});
        }
        const bool toEnd = phaseOf(mover) == MoverPhase::ToEnd;
        if (!toEnd && phaseOf(mover) != MoverPhase::ToStart) {
            continue;
        }
        const float target = toEnd ? entity->length : 0.0f;
        const float distance = toEnd ? std::min(mover.distance + entity->speed, target)
                                     : std::max(mover.distance - entity->speed, 0.0f);
        if (moveMover(id, *entity, mover, distance, settings)) {
            if (distance == target) {
                arrive(index, *entity, mover, toEnd);
            }
        } else if (!toEnd) {
            // Something is in the way of it coming back, a player under a closing door most
            // often: it goes back to the end, as doors do.
            mover.phase = static_cast<std::uint32_t>(MoverPhase::ToEnd);
        }
    }
    deliverQueued();
}

// Moves the mover to the distance along its path. Players it would move into are pushed along
// with it, and players standing on it are carried; if any of them would end up inside something
// solid, nothing moves and it returns false.
bool LevelLogic::moveMover(EntityId id, const GameLevel::Entity& entity, Mover& mover,
                           float distance, const GameSettings& settings) {
    World& world = m_state.world;
    const CollisionScene scene(m_level, world);
    const Vec3 delta = entity.moverOffset(distance) - mover.offset;

    struct Pushed {
        Transform* transform = nullptr;
        Vec3 from;
        Vec3 halfExtents;
    };
    std::array<Pushed, kMaxPushed> pushed{};
    std::size_t pushedCount = 0;
    // Riders are found before the mover moves out from under them.
    std::array<bool, kMaxPushed> riding{};
    const std::span<const PlayerMotion> motions = world.playerMotions.components();
    const std::span<const EntityId> bodies = world.playerMotions.entities();
    for (std::size_t i = 0; i < motions.size() && i < kMaxPushed; ++i) {
        const Transform* transform = world.transforms.get(bodies[i]);
        riding[i] =
            transform != nullptr &&
            standingOn(scene, movementOf(world, bodies[i], settings), *transform, motions[i]) == id;
    }

    const Mover before = mover;
    mover.offset = entity.moverOffset(distance);
    mover.distance = distance;
    bool blocked = false;
    for (std::size_t i = 0; i < motions.size() && !blocked; ++i) {
        Transform* transform = world.transforms.get(bodies[i]);
        if (transform == nullptr) {
            continue;
        }
        const Vec3 half = movementOf(world, bodies[i], settings).halfExtents;
        const bool inTheWay = entity.model != nullptr &&
                              entity.model->overlapsBox(transform->position - mover.offset, half);
        if (!inTheWay && !(i < kMaxPushed && riding[i])) {
            continue;
        }
        if (pushedCount == kMaxPushed) {
            blocked = true;
            break;
        }
        pushed[pushedCount++] = {transform, transform->position, half};
        transform->position += delta;
    }
    for (std::size_t i = 0; i < pushedCount && !blocked; ++i) {
        blocked =
            scene.overlapsBox(pushed[i].transform->position, pushed[i].halfExtents, kStuckInset);
    }
    if (blocked) {
        mover = before;
        for (std::size_t i = 0; i < pushedCount; ++i) {
            pushed[i].transform->position = pushed[i].from;
        }
    }
    return !blocked;
}

void LevelLogic::activateMovers(const GameSettings& settings) {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    const CollisionScene scene(m_level, world);
    for (std::size_t i = 0; i < world.movers.size(); ++i) {
        const EntityId id = world.movers.entities()[i];
        Mover& mover = world.movers.components()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity = levelEntityOf(id, LogicClass::Mover, index);
        if (entity == nullptr || entity->model == nullptr ||
            entity->activate == MoverActivation::Input || (mover.flags & kMoverStopped) != 0) {
            continue;
        }
        EntityId activator;
        bool playerHolds = false;
        const std::span<const PlayerMotion> motions = world.playerMotions.components();
        for (std::size_t p = 0; p < motions.size() && !playerHolds; ++p) {
            const EntityId body = world.playerMotions.entities()[p];
            const Transform* transform = world.transforms.get(body);
            if (transform == nullptr) {
                continue;
            }
            const MovementSettings& movement = movementOf(world, body, settings);
            const bool activates =
                entity->activate == MoverActivation::Touch
                    ? entity->model->overlapsBox(transform->position - mover.offset,
                                                 movement.halfExtents +
                                                     Vec3{kTouchMargin, kTouchMargin, kTouchMargin})
                    : standingOn(scene, movement, *transform, motions[p]) == id;
            if (activates) {
                activator = activator.isValid() ? activator : body;
                playerHolds = world.playerControls.has(body);
            }
        }
        if (!activator.isValid()) {
            continue;
        }
        // Anyone in a doorway keeps a door open, but only players hold a lift at its end or send
        // it back up on its way down: an enemy riding it back waits for it to go. Enemies set a
        // lift going only from rest at its start.
        const bool anyone = entity->activate == MoverActivation::Touch || playerHolds;
        if (phaseOf(mover) == MoverPhase::AtEnd) {
            if (anyone) {
                extendWait(*entity, mover);
            }
        } else if (headingToStart(mover) && (anyone || phaseOf(mover) == MoverPhase::AtStart)) {
            setOff(index, mover, true, activator);
        }
    }
    deliverQueued();
}

} // namespace ct
