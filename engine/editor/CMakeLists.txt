add_library(ct_editor STATIC
    include/croftech/editor/BrushEditing.h
    include/croftech/editor/CommandStack.h
    include/croftech/editor/EditorActions.h
    include/croftech/editor/EditorDocument.h
    include/croftech/editor/LevelCommands.h
    include/croftech/editor/LevelItem.h
    include/croftech/editor/SelectionDrag.h
    include/croftech/editor/AffineTransform.h
    src/BrushEditing.cpp
    src/CommandStack.cpp
    src/EditorActions.cpp
    src/EditorDocument.cpp
    src/LevelCommands.cpp
    src/LevelItem.cpp
    src/SelectionDrag.cpp
    src/AffineTransform.cpp
)
add_library(ct::editor ALIAS ct_editor)

target_include_directories(ct_editor PUBLIC include)
target_link_libraries(ct_editor PUBLIC ct::core ct::level)

ct_apply_defaults(ct_editor)
