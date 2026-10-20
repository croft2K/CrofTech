add_executable(ct_unit_tests
    unit/Main.cpp
    unit/core/AngleTests.cpp
    unit/core/ConfigFileTests.cpp
    unit/core/ConsoleTests.cpp
    unit/core/ContentTests.cpp
    unit/core/CVarTests.cpp
    unit/core/HashTests.cpp
    unit/level/BrushGeometryTests.cpp
    unit/sim/CollisionWorldTests.cpp
    unit/sim/MovementConfigTests.cpp
    unit/sim/WorldTests.cpp
)
target_link_libraries(ct_unit_tests PRIVATE ct::sim doctest::doctest)
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
)
target_link_libraries(ct_sim_tests PRIVATE ct::client ct::sim doctest::doctest)
ct_apply_defaults(ct_sim_tests)
ct_add_test(ct_sim_tests)
