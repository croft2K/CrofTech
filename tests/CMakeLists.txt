add_executable(ct_unit_tests
    unit/Main.cpp
    unit/core/AngleTests.cpp
    unit/core/ConfigFileTests.cpp
    unit/core/ConsoleTests.cpp
    unit/core/ContentTests.cpp
    unit/core/CVarTests.cpp
    unit/core/HashTests.cpp
    unit/core/RandomTests.cpp
    unit/core/SchemaTests.cpp
    unit/editor/BehaviourEditingTests.cpp
    unit/editor/BrushEditingTests.cpp
    unit/editor/BrushEntityTests.cpp
    unit/editor/BrushOperationsTests.cpp
    unit/editor/CornerEditingTests.cpp
    unit/editor/EditorDocumentTests.cpp
    unit/editor/EntityLinksTests.cpp
    unit/editor/GameDataTests.cpp
    unit/editor/GroupTests.cpp
    unit/editor/LogicGraphTests.cpp
    unit/editor/LuauHighlightTests.cpp
    unit/editor/ScriptLinksTests.cpp
    unit/editor/SelectionTests.cpp
    unit/editor/ShapesTests.cpp
    unit/editor/TextBufferTests.cpp
    unit/editor/AffineTransformTests.cpp
    unit/audio/MixerTests.cpp
    unit/audio/WavFileTests.cpp
    unit/level/BrushGeometryTests.cpp
    unit/level/EntityClassesTests.cpp
    unit/level/LevelFileTests.cpp
    unit/level/LevelLoadTests.cpp
    unit/level/MapImportTests.cpp
    unit/net/LoopbackLinkTests.cpp
    unit/net/ProtocolTests.cpp
    unit/script/ScriptLibraryTests.cpp
    unit/script/ScriptProgramTests.cpp
    unit/script/ScriptRunnerTests.cpp
    unit/sim/BehaviourTreeTests.cpp
    unit/sim/CollisionWorldTests.cpp
    unit/sim/GameSettingsTests.cpp
    unit/sim/LevelNavigationTests.cpp
    unit/sim/MovementConfigTests.cpp
    unit/sim/SaveGameTests.cpp
    unit/sim/StateSaveTests.cpp
    unit/sim/WorldTests.cpp
    unit/TestFiles.h
    sim/TestLevels.cpp
    sim/TestLevels.h
)
target_include_directories(ct_unit_tests PRIVATE unit ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ct_unit_tests PRIVATE ct::audio ct::editor ct::net ct::sim doctest::doctest)
target_compile_definitions(ct_unit_tests PRIVATE
    CROFTECH_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    CROFTECH_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
ct_apply_defaults(ct_unit_tests)
ct_add_test(ct_unit_tests)

# Headless simulation tests: whole-game behaviour with no window or GPU.
add_executable(ct_sim_tests
    unit/Main.cpp
    sim/BehaviourTests.cpp
    sim/DeterminismTests.cpp
    sim/EncounterTests.cpp
    sim/EnemyTests.cpp
    sim/GameEventsTests.cpp
    sim/HealthTests.cpp
    sim/LogicTests.cpp
    sim/MoverTests.cpp
    sim/MovementTests.cpp
    sim/NavigationTests.cpp
    sim/PickupTests.cpp
    sim/ScriptTests.cpp
    sim/ServerTests.cpp
    sim/SimHarness.h
    sim/SlidingTests.cpp
    sim/WeaponTests.cpp
    sim/StepSmoothingTests.cpp
    sim/TestLevels.cpp
    sim/TestLevels.h
)
target_link_libraries(ct_sim_tests PRIVATE ct::client ct::sim doctest::doctest)
target_compile_definitions(ct_sim_tests PRIVATE
    CROFTECH_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
ct_apply_defaults(ct_sim_tests)
ct_add_test(ct_sim_tests)

add_subdirectory(fuzz)
