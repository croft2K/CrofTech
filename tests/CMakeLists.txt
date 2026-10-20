add_executable(ct_unit_tests
    unit/Main.cpp
    unit/core/HashTests.cpp
)
target_link_libraries(ct_unit_tests PRIVATE ct::core doctest::doctest)
ct_apply_defaults(ct_unit_tests)
ct_add_test(ct_unit_tests)
