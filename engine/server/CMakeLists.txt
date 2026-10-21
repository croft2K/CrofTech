add_library(ct_server STATIC
    include/croftech/server/Server.h
    include/croftech/server/ServerThread.h
    src/Server.cpp
    src/ServerThread.cpp
)
add_library(ct::server ALIAS ct_server)

target_include_directories(ct_server PUBLIC include)
target_link_libraries(ct_server PUBLIC ct::net ct::sim PRIVATE ct::platform)

ct_apply_defaults(ct_server)
