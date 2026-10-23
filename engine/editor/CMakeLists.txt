add_library(ct_editor STATIC
    include/croftech/editor/BrushEditing.h
    include/croftech/editor/BrushOperations.h
    include/croftech/editor/CommandStack.h
    include/croftech/editor/EditorActions.h
    include/croftech/editor/EditorDocument.h
    include/croftech/editor/EntityLinks.h
    include/croftech/editor/GameData.h
    include/croftech/editor/LevelCommands.h
    include/croftech/editor/LevelItem.h
    include/croftech/editor/LuauHighlight.h
    include/croftech/editor/ScriptLinks.h
    include/croftech/editor/SelectionDrag.h
    include/croftech/editor/Shapes.h
    include/croftech/editor/TextBuffer.h
    include/croftech/editor/AffineTransform.h
    src/BrushEditing.cpp
    src/BrushOperations.cpp
    src/CommandStack.cpp
    src/EditorActions.cpp
    src/EditorDocument.cpp
    src/EntityLinks.cpp
    src/GameData.cpp
    src/LevelCommands.cpp
    src/LevelItem.cpp
    src/LuauHighlight.cpp
    src/ScriptLinks.cpp
    src/SelectionDrag.cpp
    src/Shapes.cpp
    src/TextBuffer.cpp
    src/AffineTransform.cpp
)
add_library(ct::editor ALIAS ct_editor)

target_include_directories(ct_editor PUBLIC include)
target_link_libraries(ct_editor PUBLIC ct::core ct::level ct::script ct::sim)

ct_apply_defaults(ct_editor)
