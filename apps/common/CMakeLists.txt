# Code shared by the client and the editor.
add_library(ct_app_common STATIC
    PlayerInput.cpp
    PlayerInput.h
    WorldMesh.cpp
    WorldMesh.h
)
target_include_directories(ct_app_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ct_app_common PUBLIC ct::level ct::render ct::sim PRIVATE SDL3::SDL3)
ct_apply_defaults(ct_app_common)
