add_library(ct_render STATIC
    include/croftech/render/Renderer.h
    src/Renderer.cpp
    src/ShaderModule.cpp
    src/ShaderModule.h
    src/UiRenderer.cpp
    src/UiRenderer.h
    src/VmaImplementation.cpp
)
add_library(ct::render ALIAS ct_render)

target_include_directories(ct_render PUBLIC include)
target_link_libraries(ct_render
    PUBLIC ct::core
    PRIVATE SDL3::SDL3 Vulkan::Headers volk::volk GPUOpen::VulkanMemoryAllocator
            vk-bootstrap::vk-bootstrap imgui::imgui)
target_compile_definitions(ct_render PRIVATE
    VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

ct_apply_defaults(ct_render)
# Vulkan structs are filled with designated initializers that name only the fields that matter;
# the rest are zero by design.
target_compile_options(ct_render PRIVATE -Wno-missing-designated-field-initializers)

# Shaders compile to SPIR-V next to the executables.
set(shader_output_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders)
set(shader_outputs)
foreach(shader world.vert world.frag ui.vert ui.frag)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader})
    set(output ${shader_output_dir}/${shader}.spv)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_output_dir}
        COMMAND ${CT_GLSLC} --target-env=vulkan1.3 -O -Werror -o ${output} ${source}
        DEPENDS ${source}
        COMMENT "Compiling shader ${shader}"
        VERBATIM)
    list(APPEND shader_outputs ${output})
endforeach()
add_custom_target(ct_shaders DEPENDS ${shader_outputs})
add_dependencies(ct_render ct_shaders)
