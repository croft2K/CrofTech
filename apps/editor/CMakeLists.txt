add_executable(croftech_editor
    EditorPanels.cpp
    EditorPanels.h
    LevelViewport.cpp
    LevelViewport.h
    main.cpp
    NavMeshView.cpp
    NavMeshView.h
    PlaySession.cpp
    PlaySession.h
)
target_link_libraries(croftech_editor PRIVATE ct_app_common ct::client ct::editor ct::devui ct::render
                                              ct::sim ct::level imgui::imgui SDL3::SDL3)
target_compile_definitions(croftech_editor PRIVATE CROFTECH_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

ct_apply_defaults(croftech_editor)
