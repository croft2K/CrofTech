add_library(ct_nav STATIC
    include/croftech/nav/NavMesh.h
    include/croftech/nav/NavQuery.h
    src/NavDetour.h
    src/NavMesh.cpp
    src/NavQuery.cpp
)
add_library(ct::nav ALIAS ct_nav)

target_include_directories(ct_nav PUBLIC include)
target_link_libraries(ct_nav
    PUBLIC ct::core ct::level
    PRIVATE RecastNavigation::Recast RecastNavigation::Detour)

ct_apply_defaults(ct_nav)
