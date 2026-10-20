add_subdirectory(core)
add_subdirectory(level)
add_subdirectory(sim)
add_subdirectory(platform)
add_subdirectory(client)

if(CROFTECH_BUILD_CLIENT)
    add_subdirectory(render)
endif()
