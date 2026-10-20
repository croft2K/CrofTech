add_library(ct_devui STATIC
    include/croftech/devui/ConsoleWindow.h
    include/croftech/devui/DevUi.h
    include/croftech/devui/StatsOverlay.h
    src/ConsoleWindow.cpp
    src/DevUi.cpp
    src/StatsOverlay.cpp
)
add_library(ct::devui ALIAS ct_devui)

target_include_directories(ct_devui PUBLIC include)
target_link_libraries(ct_devui PUBLIC ct::core PRIVATE imgui::imgui SDL3::SDL3)

ct_apply_defaults(ct_devui)
