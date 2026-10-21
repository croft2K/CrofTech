add_executable(ct_unit_tests
    unit/Main.cpp
    unit/core/AngleTests.cpp
    unit/core/ConfigFileTests.cpp
    unit/core/ConsoleTests.cpp
    unit/core/ContentTests.cpp
    unit/core/CVarTests.cpp
    unit/core/HashTests.cpp
    unit/core/SchemaTests.cpp
    unit/editor/BrushEditingTests.cpp
    unit/editor/CornerEditingTests.cpp
    unit/editor/EditorDocumentTests.cpp
    unit/editor/SelectionTests.cpp
    unit/editor/ShapesTests.cpp
    unit/editor/AffineTransformTests.cpp
    unit/level/BrushGeometryTests.cpp
    unit/level/LevelFileTests.cpp
    unit/level/MapImportTests.cpp
    unit/sim/CollisionWorldTests.cpp
    unit/sim/MovementConfigTests.cpp
    unit/sim/StateSaveTests.cpp
    unit/sim/WorldTests.cpp
    unit/TestFiles.h
    sim/TestLevels.cpp
    sim/TestLevels.h
)
target_include_directories(ct_unit_tests PRIVATE unit ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ct_unit_tests PRIVATE ct::editor ct::sim doctest::doctest)
target_compile_definitions(ct_unit_tests PRIVATE
    CROFTECH_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    CROFTECH_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
ct_apply_defaults(ct_unit_tests)
ct_add_test(ct_unit_tests)

# Headless simulation tests: whole-game behaviour with no window or GPU.
add_executable(ct_sim_tests
    unit/Main.cpp
    sim/DeterminismTests.cpp
    sim/MovementTests.cpp
    sim/SimHarness.h
    sim/SimThreadTests.cpp
    sim/SlidingTests.cpp
    sim/TestLevels.cpp
    sim/TestLevels.h
)
target_link_libraries(ct_sim_tests PRIVATE ct::client ct::sim doctest::doctest)
ct_apply_defaults(ct_sim_tests)
ct_add_test(ct_sim_tests)

add_subdirectory(fuzz)
