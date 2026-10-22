#include "LevelLogic.h"

#include "croftech/core/Profile.h"
#include "croftech/sim/CollisionScene.h"
#include "croftech/sim/Enemies.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ct {
namespace {

// Whether a living body stands where a new enemy's box would go, centred there. The dead are
// not solid, so a corpse on the spot doesn't hold the spawner up.
bool spotTaken(const World& world, const GameSettings& settings, Vec3 centre, Vec3 halfExtents) {
    for (const EntityId body : world.playerMotions.entities()) {
        const Transform* transform = world.transforms.get(body);
        const Health* health = world.healths.get(body);
        if (transform == nullptr || (health != nullptr && isDead(*health))) {
            continue;
        }
        const Vec3 other = movementOf(world, body, settings).halfExtents;
        const Vec3 apart = transform->position - centre;
        if (std::fabs(apart.x) < halfExtents.x + other.x &&
            std::fabs(apart.y) < halfExtents.y + other.y &&
            std::fabs(apart.z) < halfExtents.z + other.z) {
            return true;
        }
    }
    return false;
}

} // namespace

const GameLevel::Entity* LevelLogic::encounterOf(const Enemy& enemy, std::uint32_t& index) const {
    const GameLevel::Entity* spawner = m_level.entity(enemy.spawner);
    if (spawner == nullptr || spawner->logicClass != LogicClass::Spawner) {
        return nullptr;
    }
    const GameLevel::Entity* encounter = m_level.entity(spawner->encounter);
    if (encounter == nullptr || encounter->logicClass != LogicClass::Encounter) {
        return nullptr;
    }
    index = spawner->encounter;
    return encounter;
}

void LevelLogic::startWave(std::uint32_t index, Encounter& encounter) {
    World& world = m_state.world;
    const std::uint32_t wave = encounter.waves++;
    encounter.phase = static_cast<std::uint32_t>(EncounterPhase::Running);
    if (wave == 0) {
        fire(index, LogicEvent::OnStart, {});
    }
    fire(index, LogicEvent::OnWave, {});
    for (std::size_t i = 0; i < world.spawners.size(); ++i) {
        std::uint32_t spawnerIndex = 0;
        const GameLevel::Entity* spawner =
            levelEntityOf(world.spawners.entities()[i], LogicClass::Spawner, spawnerIndex);
        if (spawner != nullptr && spawner->encounter == index && spawner->wave == wave) {
            Spawner& waiting = world.spawners.components()[i];
            waiting.waiting = std::min(waiting.waiting + spawner->count, kMaxSpawnerWaiting);
        }
    }
}

void LevelLogic::stopEncounter(std::uint32_t index, Encounter& encounter) {
    World& world = m_state.world;
    encounter.phase = static_cast<std::uint32_t>(EncounterPhase::Stopped);
    for (std::size_t i = 0; i < world.spawners.size(); ++i) {
        std::uint32_t spawnerIndex = 0;
        const GameLevel::Entity* spawner =
            levelEntityOf(world.spawners.entities()[i], LogicClass::Spawner, spawnerIndex);
        if (spawner != nullptr && spawner->encounter == index) {
            world.spawners.components()[i].waiting = 0;
        }
    }
}

void LevelLogic::runEncounters(const GameSettings& settings) {
    CT_PROFILE_ZONE();
    World& world = m_state.world;
    const TickIndex tick = m_state.tick;

    // The living enemies of each encounter, by the index of the encounter's entity.
    std::array<std::uint32_t, kMaxEntities> alive{};
    for (std::size_t i = 0; i < world.enemies.size(); ++i) {
        const Health* health = world.healths.get(world.enemies.entities()[i]);
        std::uint32_t index = 0;
        const GameLevel::Entity* encounter = encounterOf(world.enemies.components()[i], index);
        if (encounter != nullptr && encounter->id.isValid() && health != nullptr &&
            !isDead(*health)) {
            ++alive[encounter->id.index];
        }
    }

    // Waves end and start.
    for (std::size_t i = 0; i < world.encounters.size(); ++i) {
        Encounter& encounter = world.encounters.components()[i];
        const EntityId id = world.encounters.entities()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity = levelEntityOf(id, LogicClass::Encounter, index);
        if (entity == nullptr) {
            continue;
        }
        if (encounterPhase(encounter) == EncounterPhase::Running) {
            std::uint32_t waiting = 0;
            for (std::size_t s = 0; s < world.spawners.size(); ++s) {
                std::uint32_t spawnerIndex = 0;
                const GameLevel::Entity* spawner =
                    levelEntityOf(world.spawners.entities()[s], LogicClass::Spawner, spawnerIndex);
                if (spawner != nullptr && spawner->encounter == index) {
                    waiting += world.spawners.components()[s].waiting;
                }
            }
            const bool last = encounter.waves >= entity->waveCount;
            if (waiting == 0 && alive[id.index] <= (last ? 0u : entity->nextWaveAt)) {
                if (last) {
                    encounter.phase = static_cast<std::uint32_t>(EncounterPhase::Cleared);
                    fire(index, LogicEvent::OnCleared, {});
                } else {
                    encounter.phase = static_cast<std::uint32_t>(EncounterPhase::Between);
                    encounter.nextWaveTick = tick + entity->waveDelayTicks;
                }
            }
        }
        if (encounterPhase(encounter) == EncounterPhase::Between &&
            encounter.nextWaveTick <= tick) {
            startWave(index, encounter);
        }
    }

    // Spawners send what is waiting, one each a tick, while there is room.
    for (std::size_t i = 0; i < world.spawners.size(); ++i) {
        Spawner& spawner = world.spawners.components()[i];
        std::uint32_t index = 0;
        const GameLevel::Entity* entity =
            levelEntityOf(world.spawners.entities()[i], LogicClass::Spawner, index);
        if (spawner.waiting == 0 || entity == nullptr ||
            entity->enemyType >= settings.enemies.size()) {
            continue;
        }
        const GameLevel::Entity* encounter = m_level.entity(entity->encounter);
        if (encounter != nullptr && encounter->id.isValid() &&
            encounter->logicClass == LogicClass::Encounter && encounter->maxAlive > 0 &&
            alive[encounter->id.index] >= encounter->maxAlive) {
            continue;
        }
        const MovementSettings& movement = settings.enemies[entity->enemyType].movement;
        const Vec3 centre =
            entity->position +
            Vec3{0.0f, 0.0f, movement.halfExtents.z + CollisionWorld::kSurfaceEpsilon};
        if (spotTaken(world, settings, centre, movement.halfExtents)) {
            continue;
        }
        const EntityId id = world.entities.create();
        if (!id.isValid()) {
            break; // the world is full; they wait until there is room
        }
        makeEnemy(world, id, settings, entity->enemyType, entity->position, entity->yaw);
        Enemy& enemy = *world.enemies.get(id);
        enemy.spawner = index;
        --spawner.waiting;
        if (encounter != nullptr && encounter->id.isValid()) {
            ++alive[encounter->id.index];
        }
        fire(index, LogicEvent::OnSpawn, id);
        if (entity->hunt) {
            alertEnemy(index, enemy, nearestPlayer(entity->position));
        }
    }
    deliverQueued();
}

} // namespace ct
