add_library(ct_sim STATIC
    include/croftech/sim/CollisionScene.h
    include/croftech/sim/CollisionWorld.h
    include/croftech/sim/ComponentPool.h
    include/croftech/sim/Components.h
    include/croftech/sim/Damage.h
    include/croftech/sim/Enemies.h
    include/croftech/sim/Enemy.h
    include/croftech/sim/EntityId.h
    include/croftech/sim/Firing.h
    include/croftech/sim/GameLevel.h
    include/croftech/sim/GameSettings.h
    include/croftech/sim/Health.h
    include/croftech/sim/LogicComponents.h
    include/croftech/sim/MovementConfig.h
    include/croftech/sim/PlayerMovement.h
    include/croftech/sim/Projectile.h
    include/croftech/sim/SaveGame.h
    include/croftech/sim/Simulation.h
    include/croftech/sim/StateSave.h
    include/croftech/sim/UserCmd.h
    include/croftech/sim/Weapons.h
    include/croftech/sim/World.h
    src/CollisionScene.cpp
    src/CollisionWorld.cpp
    src/Damage.cpp
    src/Encounters.cpp
    src/Enemies.cpp
    src/Firing.cpp
    src/GameLevel.cpp
    src/GameSettings.cpp
    src/LevelLogic.cpp
    src/LevelLogic.h
    src/MovementConfig.cpp
    src/Movers.cpp
    src/NavQueries.h
    src/PlayerMovement.cpp
    src/SaveGame.cpp
    src/ScriptHost.h
    src/Scripts.cpp
    src/Simulation.cpp
    src/StateSave.cpp
)
add_library(ct::sim ALIAS ct_sim)

target_include_directories(ct_sim PUBLIC include)
target_link_libraries(ct_sim PUBLIC ct::core ct::level ct::nav ct::script)

ct_apply_defaults(ct_sim)
