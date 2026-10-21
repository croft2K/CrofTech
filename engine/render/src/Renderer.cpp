#include "croftech/render/Renderer.h"

#include "ShaderModule.h"
#include "UiRenderer.h"

#include "croftech/core/Log.h"
#include "croftech/core/Matrix.h"
#include "croftech/core/Profile.h"

// clang-format off
// volk must come first: it defines the Vulkan function pointers everything else calls.
#include <volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace ct {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkClearColorValue kSkyColor{{0.66f, 0.75f, 0.86f, 1.0f}}; // already sRGB-encoded

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
};

struct FrameResources {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
};

// A WorldView with its rectangle clamped to the swapchain.
struct DrawnView {
    VkRect2D rect{};
    Mat4 viewProjection;
    bool clear = false;
    VkClearColorValue background{};
    bool drawWorld = true;
};

struct PushConstants {
    Mat4 viewProjection;
    float gridMinor = 1.0f;
    float gridMajor = 4.0f;
};

} // namespace

struct Renderer::Impl {
    SDL_Window* window = nullptr;
    vkb::Instance instance;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::Device device;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VmaAllocator allocator = nullptr;

    vkb::Swapchain swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkSemaphore> renderFinished; // one per swapchain image
    Image depth;
    bool swapchainDirty = false;

    std::array<FrameResources, kFramesInFlight> frames{};
    float gridMinor = 1.0f;
    float gridMajor = 4.0f;
    std::uint32_t frameIndex = 0;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    Buffer vertexBuffer;
    Buffer indexBuffer;
    std::uint32_t indexCount = 0;

    UiRenderer ui;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl();

    bool init(SDL_Window* sdlWindow, const std::filesystem::path& shaderDirectory,
              std::string& error);
    bool createSwapchain(std::string& error);
    void destroySwapchainResources();
    bool createPipeline(const std::filesystem::path& shaderDirectory, std::string& error);
    Buffer createHostBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& buffer);
    void recordFrame(VkCommandBuffer cmd, std::uint32_t imageIndex,
                     std::span<const DrawnView> views, const ImDrawData* uiData);
};

bool Renderer::Impl::init(SDL_Window* sdlWindow, const std::filesystem::path& shaderDirectory,
                          std::string& error) {
    window = sdlWindow;
    if (volkInitialize() != VK_SUCCESS) {
        error = "no Vulkan loader found; install or update the GPU driver";
        return false;
    }

    Uint32 extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (extensions == nullptr) {
        error = SDL_GetError();
        return false;
    }

    vkb::InstanceBuilder instanceBuilder(vkGetInstanceProcAddr);
    instanceBuilder.set_app_name("CrofTech")
        .set_engine_name("CrofTech")
        .require_api_version(1, 3, 0);
#if !defined(NDEBUG)
    instanceBuilder.request_validation_layers().use_default_debug_messenger();
#endif
    for (Uint32 i = 0; i < extensionCount; ++i) {
        instanceBuilder.enable_extension(extensions[i]);
    }
    auto instanceResult = instanceBuilder.build();
    if (!instanceResult) {
        error = "Vulkan instance: " + instanceResult.error().message();
        return false;
    }
    instance = instanceResult.value();
    volkLoadInstance(instance.instance);

    if (!SDL_Vulkan_CreateSurface(window, instance.instance, nullptr, &surface)) {
        error = SDL_GetError();
        return false;
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    vkb::PhysicalDeviceSelector selector(instance);
    auto physicalResult = selector.set_minimum_version(1, 3)
                              .set_required_features_13(features13)
                              .set_surface(surface)
                              .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                              .select();
    if (!physicalResult) {
        error = "no GPU with Vulkan 1.3: " + physicalResult.error().message();
        return false;
    }

    auto deviceResult = vkb::DeviceBuilder(physicalResult.value()).build();
    if (!deviceResult) {
        error = "Vulkan device: " + deviceResult.error().message();
        return false;
    }
    device = deviceResult.value();
    volkLoadDevice(device.device);

    auto queueResult = device.get_queue(vkb::QueueType::graphics);
    auto familyResult = device.get_queue_index(vkb::QueueType::graphics);
    if (!queueResult || !familyResult) {
        error = "no graphics queue";
        return false;
    }
    queue = queueResult.value();
    queueFamily = familyResult.value();
    VkBool32 canPresent = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device.physical_device.physical_device, queueFamily,
                                         surface, &canPresent);
    if (canPresent != VK_TRUE) {
        error = "the graphics queue cannot present to the window";
        return false;
    }
    logInfo("GPU: {}", physicalResult.value().name);

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = device.physical_device.physical_device;
    allocatorInfo.device = device.device;
    allocatorInfo.instance = instance.instance;
    allocatorInfo.pVulkanFunctions = &functions;
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        error = "failed to create the GPU memory allocator";
        return false;
    }

    for (FrameResources& frame : frames) {
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = queueFamily,
        };
        const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        const VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        if (vkCreateCommandPool(device.device, &poolInfo, nullptr, &frame.commandPool) !=
                VK_SUCCESS ||
            vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &frame.imageAvailable) !=
                VK_SUCCESS ||
            vkCreateFence(device.device, &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS) {
            error = "failed to create frame resources";
            return false;
        }
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(device.device, &allocateInfo, &frame.commandBuffer) !=
            VK_SUCCESS) {
            error = "failed to allocate command buffers";
            return false;
        }
    }

    return createSwapchain(error) && createPipeline(shaderDirectory, error) &&
           ui.init(device.device, allocator, kFramesInFlight, swapchain.image_format, kDepthFormat,
                   shaderDirectory, error);
}

bool Renderer::Impl::createSwapchain(std::string& error) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    auto result =
        vkb::SwapchainBuilder(device)
            .set_old_swapchain(swapchain)
            // UNORM, not SRGB: shaders write display-encoded colour themselves (later a
            // tone-mapping pass will). SRGB swapchains also trip a validation error on NVIDIA
            // drivers, which report no storage support for them.
            .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            // Lowest latency first: mailbox never blocks and never tears; immediate may tear.
            .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(static_cast<std::uint32_t>(width),
                                static_cast<std::uint32_t>(height))
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            .build();
    if (!result) {
        error = "swapchain: " + result.error().message();
        return false;
    }
    destroySwapchainResources();
    const bool firstSwapchain = swapchain.swapchain == VK_NULL_HANDLE;
    swapchain = result.value();
    swapchainImages = swapchain.get_images().value();
    swapchainViews = swapchain.get_image_views().value();
    if (firstSwapchain) {
        const char* mode = swapchain.present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox"
                           : swapchain.present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR
                               ? "immediate"
                               : "fifo (vsync)";
        logInfo("Present mode: {}", mode);
    }

    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished.resize(swapchainImages.size());
    for (VkSemaphore& semaphore : renderFinished) {
        vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &semaphore);
    }

    const VkImageCreateInfo depthInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = kDepthFormat,
        .extent = {swapchain.extent.width, swapchain.extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    };
    VmaAllocationCreateInfo depthAllocation{};
    depthAllocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    depthAllocation.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    if (vmaCreateImage(allocator, &depthInfo, &depthAllocation, &depth.image, &depth.allocation,
                       nullptr) != VK_SUCCESS) {
        error = "failed to create the depth buffer";
        return false;
    }
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = kDepthFormat,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };
    vkCreateImageView(device.device, &viewInfo, nullptr, &depth.view);
    swapchainDirty = false;
    return true;
}

void Renderer::Impl::destroySwapchainResources() {
    for (VkSemaphore semaphore : renderFinished) {
        vkDestroySemaphore(device.device, semaphore, nullptr);
    }
    renderFinished.clear();
    if (depth.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device.device, depth.view, nullptr);
        vmaDestroyImage(allocator, depth.image, depth.allocation);
        depth = {};
    }
    if (swapchain.swapchain != VK_NULL_HANDLE) {
        swapchain.destroy_image_views(swapchainViews);
        vkb::destroy_swapchain(swapchain);
    }
    swapchainViews.clear();
    swapchainImages.clear();
}

bool Renderer::Impl::createPipeline(const std::filesystem::path& shaderDirectory,
                                    std::string& error) {
    const VkShaderModule vertexShader =
        loadShaderModule(device.device, shaderDirectory / "world.vert.spv", error);
    const VkShaderModule fragmentShader =
        loadShaderModule(device.device, shaderDirectory / "world.frag.spv", error);
    const auto destroyModules = [&] {
        vkDestroyShaderModule(device.device, vertexShader, nullptr);
        vkDestroyShaderModule(device.device, fragmentShader, nullptr);
    };
    if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
        destroyModules();
        return false;
    }

    const VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                        0, sizeof(PushConstants)};
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    vkCreatePipelineLayout(device.device, &layoutInfo, nullptr, &pipelineLayout);

    const std::array stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
        },
    };
    const VkVertexInputBindingDescription binding{0, sizeof(WorldVertex),
                                                  VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(WorldVertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(WorldVertex, normal)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(WorldVertex, color)},
    };
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    // Brush faces wind counter-clockwise seen from outside; the projection's Y flip keeps the image
    // unmirrored, so they stay counter-clockwise on screen.
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL, // reverse-Z
    };
    const VkPipelineColorBlendAttachmentState blendAttachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const VkFormat colorFormat = swapchain.image_format;
    const VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = kDepthFormat,
    };
    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
    };
    const VkResult result = vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1,
                                                      &pipelineInfo, nullptr, &pipeline);
    destroyModules();
    if (result != VK_SUCCESS) {
        error = "failed to create the world pipeline";
        return false;
    }
    return true;
}

Buffer Renderer::Impl::createHostBuffer(const void* data, VkDeviceSize size,
                                        VkBufferUsageFlags usage) {
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
    };
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    Buffer buffer;
    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &buffer.buffer, &buffer.allocation,
                        &allocated) != VK_SUCCESS) {
        return {};
    }
    std::memcpy(allocated.pMappedData, data, static_cast<std::size_t>(size));
    vmaFlushAllocation(allocator, buffer.allocation, 0, size);
    return buffer;
}

void Renderer::Impl::destroyBuffer(Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

void Renderer::Impl::recordFrame(VkCommandBuffer cmd, std::uint32_t imageIndex,
                                 std::span<const DrawnView> views, const ImDrawData* uiData) {
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);
    ui.prepare(cmd, frameIndex, uiData);

    const std::array toRender{
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = swapchainImages[imageIndex],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        },
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image = depth.image,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        },
    };
    const VkDependencyInfo toRenderDependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<std::uint32_t>(toRender.size()),
        .pImageMemoryBarriers = toRender.data(),
    };
    vkCmdPipelineBarrier2(cmd, &toRenderDependency);

    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = kSkyColor},
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {0.0f, 0}}, // reverse-Z: 0 is infinitely far
    };
    const VkExtent2D extent = swapchain.extent;
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(cmd, &renderingInfo);

    for (const DrawnView& view : views) {
        const VkRect2D& rect = view.rect;
        if (rect.extent.width == 0 || rect.extent.height == 0) {
            continue;
        }
        const VkViewport viewport{static_cast<float>(rect.offset.x),
                                  static_cast<float>(rect.offset.y),
                                  static_cast<float>(rect.extent.width),
                                  static_cast<float>(rect.extent.height),
                                  0.0f,
                                  1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &rect);
        if (view.clear) {
            const VkClearAttachment clearColor{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = 0,
                .clearValue = {.color = view.background},
            };
            const VkClearRect clearRect{rect, 0, 1};
            vkCmdClearAttachments(cmd, 1, &clearColor, 1, &clearRect);
        }
        if (indexCount > 0 && view.drawWorld) {
            const PushConstants push{view.viewProjection, gridMinor, gridMajor};
            const VkDeviceSize offset = 0;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdPushConstants(cmd, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(push), &push);
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        }
    }
    ui.draw(cmd, frameIndex, uiData);

    vkCmdEndRendering(cmd);

    const VkImageMemoryBarrier2 toPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchainImages[imageIndex],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkDependencyInfo toPresentDependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toPresent,
    };
    vkCmdPipelineBarrier2(cmd, &toPresentDependency);
    vkEndCommandBuffer(cmd);
}

Renderer::Impl::~Impl() {
    if (device.device == VK_NULL_HANDLE) {
        if (surface != VK_NULL_HANDLE) {
            vkb::destroy_surface(instance, surface);
        }
        if (instance.instance != VK_NULL_HANDLE) {
            vkb::destroy_instance(instance);
        }
        return;
    }
    vkDeviceWaitIdle(device.device);
    ui.destroy();
    destroyBuffer(vertexBuffer);
    destroyBuffer(indexBuffer);
    vkDestroyPipeline(device.device, pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, pipelineLayout, nullptr);
    for (FrameResources& frame : frames) {
        vkDestroyFence(device.device, frame.inFlight, nullptr);
        vkDestroySemaphore(device.device, frame.imageAvailable, nullptr);
        vkDestroyCommandPool(device.device, frame.commandPool, nullptr);
    }
    if (allocator != nullptr) {
        destroySwapchainResources();
        vmaDestroyAllocator(allocator);
    }
    vkb::destroy_device(device);
    vkb::destroy_surface(instance, surface);
    vkb::destroy_instance(instance);
}

Renderer::Renderer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Renderer::~Renderer() = default;

std::unique_ptr<Renderer> Renderer::create(SDL_Window* window,
                                           const std::filesystem::path& shaderDirectory,
                                           std::string& error) {
    auto impl = std::make_unique<Impl>();
    if (!impl->init(window, shaderDirectory, error)) {
        return nullptr;
    }
    return std::make_unique<Renderer>(std::move(impl));
}

void Renderer::setWorldMesh(std::span<const WorldVertex> vertices,
                            std::span<const std::uint32_t> indices) {
    Impl& r = *m_impl;
    vkDeviceWaitIdle(r.device.device);
    r.destroyBuffer(r.vertexBuffer);
    r.destroyBuffer(r.indexBuffer);
    r.indexCount = 0;
    if (vertices.empty() || indices.empty()) {
        return;
    }
    r.vertexBuffer = r.createHostBuffer(vertices.data(), vertices.size_bytes(),
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    r.indexBuffer =
        r.createHostBuffer(indices.data(), indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (r.vertexBuffer.buffer != VK_NULL_HANDLE && r.indexBuffer.buffer != VK_NULL_HANDLE) {
        r.indexCount = static_cast<std::uint32_t>(indices.size());
    }
}

void Renderer::setWorldGrid(float minorSpacing, float majorSpacing) {
    m_impl->gridMinor = minorSpacing;
    m_impl->gridMajor = majorSpacing;
}

void Renderer::drawFrame(const Camera& camera, const ImDrawData* ui, const PixelRect* worldRect) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_impl->window, &width, &height);
    WorldView view;
    view.rect = worldRect != nullptr
                    ? *worldRect
                    : PixelRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
    const float aspect = view.rect.height > 0.0f ? view.rect.width / view.rect.height : 1.0f;
    view.viewProjection = makeViewProjection(camera, aspect);
    drawFrame(std::span<const WorldView>(&view, 1), ui);
}

void Renderer::drawFrame(std::span<const WorldView> views, const ImDrawData* ui) {
    CT_PROFILE_ZONE();
    Impl& r = *m_impl;
    const VkDevice device = r.device.device;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(r.window, &width, &height);
    // A minimised window keeps its size in SDL but has a 0 x 0 surface, which no swapchain fits.
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(r.window) & SDL_WINDOW_MINIMIZED) != 0) {
        return;
    }
    if (r.swapchainDirty || static_cast<std::uint32_t>(width) != r.swapchain.extent.width ||
        static_cast<std::uint32_t>(height) != r.swapchain.extent.height) {
        vkDeviceWaitIdle(device);
        std::string error;
        if (!r.createSwapchain(error)) {
            logError("{}", error);
            return;
        }
    }

    FrameResources& frame = r.frames[r.frameIndex];
    vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);

    std::uint32_t imageIndex = 0;
    const VkResult acquired =
        vkAcquireNextImageKHR(device, r.swapchain.swapchain, UINT64_MAX, frame.imageAvailable,
                              VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        r.swapchainDirty = true;
        return;
    }
    if (acquired == VK_SUBOPTIMAL_KHR) {
        r.swapchainDirty = true;
    } else if (acquired != VK_SUCCESS) {
        logError("failed to acquire a swapchain image ({})", static_cast<int>(acquired));
        return;
    }

    vkResetFences(device, 1, &frame.inFlight);
    vkResetCommandPool(device, frame.commandPool, 0);
    const VkExtent2D extent = r.swapchain.extent;
    const auto clampTo = [](float value, std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::clamp(value, 0.0f, static_cast<float>(limit)));
    };
    std::vector<DrawnView> drawn;
    for (const WorldView& view : views) {
        const std::uint32_t x0 = clampTo(view.rect.x, extent.width);
        const std::uint32_t y0 = clampTo(view.rect.y, extent.height);
        const std::uint32_t x1 = clampTo(view.rect.x + view.rect.width, extent.width);
        const std::uint32_t y1 = clampTo(view.rect.y + view.rect.height, extent.height);
        DrawnView out;
        out.rect = {{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
                    {x1 - x0, y1 - y0}};
        out.viewProjection = view.viewProjection;
        out.clear = view.clearBackground;
        out.drawWorld = view.drawWorld;
        out.background = {{view.background.x, view.background.y, view.background.z, 1.0f}};
        drawn.push_back(out);
    }
    r.recordFrame(frame.commandBuffer, imageIndex, drawn, ui);

    const VkSemaphoreSubmitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAvailable,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = r.renderFinished[imageIndex],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkCommandBufferSubmitInfo commandInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo,
    };
    vkQueueSubmit2(r.queue, 1, &submitInfo, frame.inFlight);

    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &r.renderFinished[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &r.swapchain.swapchain,
        .pImageIndices = &imageIndex,
    };
    const VkResult presented = vkQueuePresentKHR(r.queue, &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        r.swapchainDirty = true;
    }
    r.frameIndex = (r.frameIndex + 1) % kFramesInFlight;
}

} // namespace ct
