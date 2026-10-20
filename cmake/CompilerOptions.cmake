# Shared compiler settings for every CrofTech target.
#
#   ct_apply_defaults(<target>)  warnings, float determinism flags, sanitizers, asserts
#   ct_add_test(<target>)        registers a test executable with CTest

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(CT_CLANG_CL TRUE)
endif()

# On Windows the ASan runtime is a DLL next to clang's resource dir; tests need it on PATH.
if(CROFTECH_SANITIZE AND CT_CLANG_CL)
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} /clang:-print-resource-dir
        OUTPUT_VARIABLE CT_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(CT_ASAN_RUNTIME_DIR "${CT_CLANG_RESOURCE_DIR}/lib/windows")
endif()

function(ct_apply_defaults target)
    if(CT_CLANG_CL)
        target_compile_options(${target} PRIVATE
            /W4 -Wconversion -Wsign-conversion -Wshadow
            # Same build + same inputs must give bit-identical state: no fused multiply-add contraction.
            /clang:-ffp-contract=off)
        if(CROFTECH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        if(CROFTECH_SANITIZE)
            target_compile_options(${target} PRIVATE -fsanitize=address)
            # CMake invokes the linker directly, so the clang driver never adds the ASan runtime.
            target_link_options(${target} PRIVATE
                "/LIBPATH:${CT_ASAN_RUNTIME_DIR}"
                clang_rt.asan_dynamic-x86_64.lib
                /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /fp:precise)
        if(CROFTECH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        if(CROFTECH_SANITIZE)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
            -ffp-contract=off)
        if(CROFTECH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(CROFTECH_SANITIZE)
            set(sanitize_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
            target_compile_options(${target} PRIVATE ${sanitize_flags})
            target_link_options(${target} PRIVATE ${sanitize_flags})
        endif()
    endif()

    # Asserts stay on in sanitizer builds even though they use an optimised config.
    target_compile_definitions(${target} PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<BOOL:${CROFTECH_SANITIZE}>>:CT_ASSERTS_ENABLED=1>)
endfunction()

function(ct_add_test target)
    add_test(NAME ${target} COMMAND ${target})
    if(CT_ASAN_RUNTIME_DIR)
        set_tests_properties(${target} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CT_ASAN_RUNTIME_DIR}")
    endif()
endfunction()
