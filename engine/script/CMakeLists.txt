add_library(ct_script STATIC
    include/croftech/script/ScriptFiles.h
    include/croftech/script/ScriptLibrary.h
    include/croftech/script/ScriptProgram.h
    include/croftech/script/ScriptRunner.h
    src/ScriptArena.cpp
    src/ScriptArena.h
    src/ScriptFiles.cpp
    src/ScriptLibrary.cpp
    src/ScriptProgram.cpp
    src/ScriptRunner.cpp
    src/ScriptSandbox.h
)
add_library(ct::script ALIAS ct_script)

target_include_directories(ct_script PUBLIC include)
target_link_libraries(ct_script
    PUBLIC ct::core ct::level
    PRIVATE unofficial::luau::Luau.VM unofficial::luau::Luau.Compiler unofficial::luau::Luau.Ast)

ct_apply_defaults(ct_script)
