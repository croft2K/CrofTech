add_library(ct_client STATIC
    include/croftech/client/ClientConnection.h
    include/croftech/client/LocalGame.h
    include/croftech/client/StepSmoothing.h
    src/ClientConnection.cpp
    src/LocalGame.cpp
    src/StepSmoothing.cpp
)
add_library(ct::client ALIAS ct_client)

target_include_directories(ct_client PUBLIC include)
target_link_libraries(ct_client PUBLIC ct::net ct::server ct::sim)

ct_apply_defaults(ct_client)
