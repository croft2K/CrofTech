add_library(ct_sim STATIC
    include/croftech/sim/CollisionScene.h
    include/croftech/sim/CollisionWorld.h
    include/croftech/sim/ComponentPool.h
    include/croftech/sim/Components.h
    include/croftech/sim/EntityId.h
    include/croftech/sim/GameLevel.h
    include/croftech/sim/LogicComponents.h
    include/croftech/sim/MovementConfig.h
    include/croftech/sim/PlayerMovement.h
    include/croftech/sim/Simulation.h
    include/croftech/sim/StateSave.h
    include/croftech/sim/UserCmd.h
    include/croftech/sim/World.h
    src/CollisionScene.cpp
    src/CollisionWorld.cpp
    src/GameLevel.cpp
    src/LevelLogic.cpp
    src/LevelLogic.h
    src/MovementConfig.cpp
    src/Movers.cpp
    src/PlayerMovement.cpp
    src/Simulation.cpp
    src/StateSave.cpp
)
add_library(ct::sim ALIAS ct_sim)

target_include_directories(ct_sim PUBLIC include)
target_link_libraries(ct_sim PUBLIC ct::core ct::level)

ct_apply_defaults(ct_sim)
