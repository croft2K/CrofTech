add_library(ct_net STATIC
    include/croftech/net/LoopbackLink.h
    include/croftech/net/Protocol.h
    include/croftech/net/Transport.h
    src/LoopbackLink.cpp
    src/Protocol.cpp
)
add_library(ct::net ALIAS ct_net)

target_include_directories(ct_net PUBLIC include)
target_link_libraries(ct_net PUBLIC ct::core ct::sim)

ct_apply_defaults(ct_net)
