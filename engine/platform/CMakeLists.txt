add_library(ct_platform STATIC
    include/croftech/platform/PreciseSleeper.h
    src/PreciseSleeper.cpp
)
add_library(ct::platform ALIAS ct_platform)

target_include_directories(ct_platform PUBLIC include)
target_link_libraries(ct_platform PUBLIC ct::core)

ct_apply_defaults(ct_platform)
