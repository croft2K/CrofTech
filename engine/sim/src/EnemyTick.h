#pragma once

#include "croftech/core/Math.h"
#include "croftech/sim/BehaviourTree.h"
#include "croftech/sim/Enemy.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/GameSettings.h"

#include <array>
#include <cstdint>

namespace ct {

class CollisionScene;
class NavQuery;
struct NavFilter;
struct Transform;
struct Health;

// One enemy's turn to think: who it is, and what the nodes of its tree have decided so far this
// tick (Behaviours.cpp walks the tree, Enemies.cpp does the conditions and actions). runEnemies
// then turns it into an input command.
struct EnemyTick {
    EnemyTick(EntityId id, Enemy& enemy, EnemyBehaviour& behaviour, const EnemyTuning& type,
              Transform& transform, const Health& health, const CollisionScene& scene,
              const GameSettings& settings, NavQuery& query, const NavFilter& filter,
              const BehaviourTree& tree)
        : id(id), enemy(enemy), behaviour(behaviour), type(type), transform(transform),
          health(health), scene(scene), settings(settings), query(query), filter(filter),
          tree(tree) {}

    EntityId id;
    Enemy& enemy;
    EnemyBehaviour& behaviour;
    const EnemyTuning& type;
    Transform& transform;
    const Health& health;
    const CollisionScene& scene;
    const GameSettings& settings;
    NavQuery& query;
    const NavFilter& filter;
    const BehaviourTree& tree;
    std::uint32_t wiring = 0; // the level entity its events go to
    bool wantsCover = false;  // hurt in a fight, or ended a burst: it may run for cover

    // The way to go across the ground (zero to stand), at a share of its running speed; the way
    // to face (zero for the way it goes, or else its look yaw); whether to pitch its aim along
    // look; and whether to shoot once it has moved.
    Vec3 heading;
    float pace = 1.0f;
    Vec3 look;
    bool aim = false;
    bool shoot = false;

    // The nodes reached this tick, and those of them that didn't fail.
    std::array<std::uint32_t, kMaxBehaviourNodes / 32> reached{};
    std::array<std::uint32_t, kMaxBehaviourNodes / 32> active{};

    // What it makes of its target, worked out the first time a node asks (LevelLogic::viewTarget)
    // and again if the target changes.
    EntityId viewed;
    bool viewDone = false;
    bool visible = false;
    bool level = false;    // overlapping it in height
    float gap = 0.0f;      // between their boxes across the ground
    float distance = 0.0f; // between their middles
    bool inReach = false;  // of a swing
    bool inRange = false;  // of its weapon
};

} // namespace ct
