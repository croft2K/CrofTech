add_library(ct_audio STATIC
    include/croftech/audio/Mixer.h
    include/croftech/audio/Sound.h
    include/croftech/audio/WavFile.h
    src/Mixer.cpp
    src/WavFile.cpp
)
add_library(ct::audio ALIAS ct_audio)

target_include_directories(ct_audio PUBLIC include)
target_link_libraries(ct_audio PUBLIC ct::core)

ct_apply_defaults(ct_audio)
