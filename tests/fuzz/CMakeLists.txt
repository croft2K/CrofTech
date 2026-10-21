# Fuzz targets for parsers of untrusted data. With CROFTECH_FUZZ (the linux-fuzz preset) they are
# libFuzzer executables; otherwise a small driver replays each seed corpus as an ordinary test.
#
#   ct_add_fuzzer(<name> CORPUS <folders> SOURCES <files> LIBS <targets>)
function(ct_add_fuzzer name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "CORPUS;SOURCES;LIBS")
    add_executable(${name} ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE ${arg_LIBS})
    if(CROFTECH_FUZZ)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    else()
        target_sources(${name} PRIVATE FuzzMain.cpp)
        ct_add_test(${name} ${arg_CORPUS})
    endif()
    ct_apply_defaults(${name})
endfunction()

ct_add_fuzzer(ct_level_file_fuzz
    CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/level ${PROJECT_SOURCE_DIR}/content/levels
    SOURCES LevelFileFuzz.cpp
    LIBS ct::level)
ct_add_fuzzer(ct_map_import_fuzz
    CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/map
    SOURCES MapImportFuzz.cpp
    LIBS ct::level)
ct_add_fuzzer(ct_state_save_fuzz
    CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/state
    SOURCES StateSaveFuzz.cpp
    LIBS ct::sim)
ct_add_fuzzer(ct_protocol_fuzz
    CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/protocol
    SOURCES ProtocolFuzz.cpp
    LIBS ct::net)
