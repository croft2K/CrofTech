add_library(ct_core STATIC
    include/croftech/core/Angle.h
    include/croftech/core/Assert.h
    include/croftech/core/ConfigFile.h
    include/croftech/core/Content.h
    include/croftech/core/Hash.h
    include/croftech/core/Log.h
    include/croftech/core/Math.h
    include/croftech/core/Matrix.h
    include/croftech/core/Profile.h
    src/Assert.cpp
    src/ConfigFile.cpp
    src/Content.cpp
    src/Log.cpp
)
add_library(ct::core ALIAS ct_core)

target_include_directories(ct_core PUBLIC include)

if(CROFTECH_PROFILE)
    target_link_libraries(ct_core PUBLIC Tracy::TracyClient)
    target_compile_definitions(ct_core PUBLIC CROFTECH_PROFILE=1)
endif()

ct_apply_defaults(ct_core)
