add_library(ct_level STATIC
    include/croftech/level/BrushGeometry.h
    include/croftech/level/Level.h
    src/BrushGeometry.cpp
    src/Level.cpp
)
add_library(ct::level ALIAS ct_level)

target_include_directories(ct_level PUBLIC include)
target_link_libraries(ct_level PUBLIC ct::core)

ct_apply_defaults(ct_level)
