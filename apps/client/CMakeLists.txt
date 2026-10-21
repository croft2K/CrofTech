add_executable(croftech
    main.cpp
)
target_link_libraries(croftech PRIVATE ct_app_common ct::client ct::devui ct::render ct::sim
                                       ct::level SDL3::SDL3)
# Development builds read engine and game content straight from the source tree, so edits
# hot-reload in place.
target_compile_definitions(croftech PRIVATE CROFTECH_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

ct_apply_defaults(croftech)
