# Code shared by the client and the editor.
add_library(ct_app_common STATIC
    EntityShapes.cpp
    EntityShapes.h
    GameHud.cpp
    GameHud.h
    PlayerInput.cpp
    PlayerInput.h
    SaveFiles.cpp
    SaveFiles.h
    WorldMesh.cpp
    WorldMesh.h
)
target_include_directories(ct_app_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ct_app_common PUBLIC ct::client ct::level ct::render ct::sim
                                     PRIVATE imgui::imgui SDL3::SDL3)
ct_apply_defaults(ct_app_common)
