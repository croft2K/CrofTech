add_library(ct_level STATIC
    include/croftech/level/BrushGeometry.h
    include/croftech/level/EntityClasses.h
    include/croftech/level/Level.h
    include/croftech/level/LevelFile.h
    include/croftech/level/LevelLoad.h
    include/croftech/level/MapImport.h
    src/BrushGeometry.cpp
    src/EntityClasses.cpp
    src/Level.cpp
    src/LevelFile.cpp
    src/LevelLoad.cpp
    src/MapImport.cpp
)
add_library(ct::level ALIAS ct_level)

target_include_directories(ct_level PUBLIC include)
target_link_libraries(ct_level PUBLIC ct::core)

ct_apply_defaults(ct_level)
