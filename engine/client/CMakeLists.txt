add_library(ct_client STATIC
    include/croftech/client/SimThread.h
    src/SimThread.cpp
)
add_library(ct::client ALIAS ct_client)

target_include_directories(ct_client PUBLIC include)
target_link_libraries(ct_client PUBLIC ct::sim PRIVATE ct::platform)

ct_apply_defaults(ct_client)
