#include "UiRenderer.h"

#include "ShaderModule.h"

#include "croftech/core/Log.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace ct {
namespace {

constexpr std::uint32_t kMaxTextures = 64;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM; // ImGui colours are display-encoded
constexpr VkDeviceSize kMinGeometryBytes = 64 * 1024;

struct PushConstants {
    float scale[2];
    float translate[2];
};

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = from,
        .newLayout = to,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

UiRenderer::~UiRenderer() { destroy(); }

bool UiRenderer::init(VkDevice device, VmaAllocator allocator, std::uint32_t framesInFlight,
                      VkFormat colorFormat, VkFormat depthFormat,
                      const std::filesystem::path& shaderDirectory, std::string& error) {
    m_device = device;
    m_allocator = allocator;
    m_frames.resize(framesInFlight);

    const VkDescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo setLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = kMaxTextures,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    if (vkCreateDescriptorSetLayout(m_device, &setLayoutInfo, nullptr, &m_setLayout) !=
            VK_SUCCESS ||
        vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS ||
        vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        error = "failed to create UI descriptor objects";
        return false;
    }
    return createPipeline(colorFormat, depthFormat, shaderDirectory, error);
}

bool UiRenderer::createPipeline(VkFormat colorFormat, VkFormat depthFormat,
                                const std::filesystem::path& shaderDirectory, std::string& error) {
    const VkShaderModule vertexShader =
        loadShaderModule(m_device, shaderDirectory / "ui.vert.spv", error);
    const VkShaderModule fragmentShader =
        loadShaderModule(m_device, shaderDirectory / "ui.frag.spv", error);
    const auto destroyModules = [&] {
        vkDestroyShaderModule(m_device, vertexShader, nullptr);
        vkDestroyShaderModule(m_device, fragmentShader, nullptr);
    };
    if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
        destroyModules();
        return false;
    }

    const VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        destroyModules();
        error = "failed to create the UI pipeline layout";
        return false;
    }

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
    const VkVertexInputBindingDescription binding{0, sizeof(ImDrawVert),
                                                  VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R8G8B8A8_UNORM,
                                          offsetof(ImDrawVert, col)},
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
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    // The pass has a depth attachment, so the pipeline declares it, but the UI ignores depth.
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };
    const VkPipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
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
    const VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat,
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
        .layout = m_pipelineLayout,
    };
    const VkResult result =
        vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    destroyModules();
    if (result != VK_SUCCESS) {
        error = "failed to create the UI pipeline";
        return false;
    }
    return true;
}

void UiRenderer::destroy() {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }
    while (!m_textures.empty()) {
        Texture* texture = m_textures.back().get();
        if (texture->owner != nullptr) {
            texture->owner->SetTexID(ImTextureID_Invalid);
            texture->owner->BackendUserData = nullptr;
            texture->owner->SetStatus(ImTextureStatus_Destroyed);
        }
        destroyTexture(texture);
    }
    for (FrameData& frame : m_frames) {
        destroyBuffer(frame.vertices);
        destroyBuffer(frame.indices);
        for (Buffer& upload : frame.uploads) {
            destroyBuffer(upload);
        }
    }
    m_frames.clear();
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroySampler(m_device, m_sampler, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    m_device = VK_NULL_HANDLE;
}

UiRenderer::Buffer UiRenderer::createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
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
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &buffer.buffer,
                        &buffer.allocation, &allocated) != VK_SUCCESS) {
        return {};
    }
    buffer.mapped = allocated.pMappedData;
    buffer.size = size;
    return buffer;
}

void UiRenderer::destroyBuffer(Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

bool UiRenderer::ensureCapacity(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (buffer.size >= size) {
        return true;
    }
    // The frame that last used this buffer has finished, so it can be replaced right away.
    destroyBuffer(buffer);
    buffer = createHostBuffer(std::max({size, buffer.size * 2, kMinGeometryBytes}), usage);
    return buffer.buffer != VK_NULL_HANDLE;
}

UiRenderer::Texture* UiRenderer::createTexture(ImTextureData& data) {
    auto texture = std::make_unique<Texture>();
    texture->owner = &data;
    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = kTextureFormat,
        .extent = {static_cast<std::uint32_t>(data.Width), static_cast<std::uint32_t>(data.Height),
                   1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(m_allocator, &imageInfo, &allocationInfo, &texture->image,
                       &texture->allocation, nullptr) != VK_SUCCESS) {
        logError("failed to create a {}x{} UI texture", data.Width, data.Height);
        return nullptr;
    }
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = kTextureFormat,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkDescriptorSetAllocateInfo setInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_setLayout,
    };
    Texture* result = texture.get();
    m_textures.push_back(std::move(texture));
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &result->view) != VK_SUCCESS ||
        vkAllocateDescriptorSets(m_device, &setInfo, &result->descriptorSet) != VK_SUCCESS) {
        logError("failed to create a UI texture view (more than {} UI textures?)", kMaxTextures);
        destroyTexture(result);
        return nullptr;
    }
    const VkDescriptorImageInfo descriptorImage{m_sampler, result->view,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = result->descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptorImage,
    };
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return result;
}

void UiRenderer::destroyTexture(Texture* texture) {
    if (texture->descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &texture->descriptorSet);
    }
    if (texture->view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, texture->view, nullptr);
    }
    if (texture->image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, texture->image, texture->allocation);
    }
    std::erase_if(m_textures, [texture](const auto& owned) { return owned.get() == texture; });
}

void UiRenderer::updateTexture(VkCommandBuffer cmd, FrameData& frame, ImTextureData& data) {
    auto* texture = static_cast<Texture*>(data.BackendUserData);

    if (data.Status == ImTextureStatus_WantDestroy) {
        // Frames still in flight may sample it until it has gone unused for that many frames.
        if (data.UnusedFrames < static_cast<int>(m_frames.size())) {
            return;
        }
        if (texture != nullptr) {
            destroyTexture(texture);
        }
        data.SetTexID(ImTextureID_Invalid);
        data.BackendUserData = nullptr;
        data.SetStatus(ImTextureStatus_Destroyed);
        return;
    }

    const bool create = data.Status == ImTextureStatus_WantCreate;
    if (create) {
        texture = createTexture(data);
        if (texture == nullptr) {
            return; // asked again next frame
        }
        data.BackendUserData = texture;
        data.SetTexID(
            static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->descriptorSet)));
    }
    if (texture == nullptr) {
        return;
    }

    const int x = create ? 0 : data.UpdateRect.x;
    const int y = create ? 0 : data.UpdateRect.y;
    const int width = create ? data.Width : data.UpdateRect.w;
    const int height = create ? data.Height : data.UpdateRect.h;
    const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(width) * 4;
    Buffer upload = createHostBuffer(rowBytes * static_cast<VkDeviceSize>(height),
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (upload.buffer == VK_NULL_HANDLE) {
        logError("failed to allocate a UI texture upload");
        return;
    }
    auto* out = static_cast<std::uint8_t*>(upload.mapped);
    for (int row = 0; row < height; ++row) {
        const auto* in = static_cast<const std::uint8_t*>(data.GetPixelsAt(x, y + row));
        if (data.Format == ImTextureFormat_RGBA32) {
            std::memcpy(out, in, static_cast<std::size_t>(rowBytes));
        } else {
            for (int i = 0; i < width; ++i) {
                out[i * 4 + 0] = 255;
                out[i * 4 + 1] = 255;
                out[i * 4 + 2] = 255;
                out[i * 4 + 3] = in[i];
            }
        }
        out += rowBytes;
    }
    vmaFlushAllocation(m_allocator, upload.allocation, 0, VK_WHOLE_SIZE);

    // Earlier frames may still be sampling the texture; the barrier orders this copy after them.
    transitionImage(cmd, texture->image,
                    create ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {x, y, 0},
        .imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1},
    };
    vkCmdCopyBufferToImage(cmd, upload.buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);
    transitionImage(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    frame.uploads.push_back(upload);
    data.SetStatus(ImTextureStatus_OK);
}

void UiRenderer::prepare(VkCommandBuffer cmd, std::uint32_t frameIndex, const ImDrawData* data) {
    FrameData& frame = m_frames[frameIndex];
    for (Buffer& upload : frame.uploads) {
        destroyBuffer(upload);
    }
    frame.uploads.clear();
    if (data == nullptr) {
        return;
    }

    if (data->Textures != nullptr) {
        for (ImTextureData* texture : *data->Textures) {
            if (texture->Status != ImTextureStatus_OK) {
                updateTexture(cmd, frame, *texture);
            }
        }
    }

    if (data->TotalVtxCount == 0) {
        return;
    }
    const auto vertexBytes = static_cast<VkDeviceSize>(data->TotalVtxCount) * sizeof(ImDrawVert);
    const auto indexBytes = static_cast<VkDeviceSize>(data->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!ensureCapacity(frame.vertices, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !ensureCapacity(frame.indices, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        logError("failed to allocate UI geometry buffers");
        return;
    }
    auto* vertices = static_cast<ImDrawVert*>(frame.vertices.mapped);
    auto* indices = static_cast<ImDrawIdx*>(frame.indices.mapped);
    for (const ImDrawList* list : data->CmdLists) {
        std::memcpy(vertices, list->VtxBuffer.Data,
                    static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        std::memcpy(indices, list->IdxBuffer.Data,
                    static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
        vertices += list->VtxBuffer.Size;
        indices += list->IdxBuffer.Size;
    }
    vmaFlushAllocation(m_allocator, frame.vertices.allocation, 0, VK_WHOLE_SIZE);
    vmaFlushAllocation(m_allocator, frame.indices.allocation, 0, VK_WHOLE_SIZE);
}

void UiRenderer::bindState(VkCommandBuffer cmd, const FrameData& frame, const ImDrawData& data) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, frame.indices.buffer, 0,
                         sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    const VkViewport viewport{0.0f,
                              0.0f,
                              data.DisplaySize.x * data.FramebufferScale.x,
                              data.DisplaySize.y * data.FramebufferScale.y,
                              0.0f,
                              1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    // Maps ImGui's display rectangle onto clip space.
    PushConstants push{};
    push.scale[0] = 2.0f / data.DisplaySize.x;
    push.scale[1] = 2.0f / data.DisplaySize.y;
    push.translate[0] = -1.0f - data.DisplayPos.x * push.scale[0];
    push.translate[1] = -1.0f - data.DisplayPos.y * push.scale[1];
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
}

void UiRenderer::draw(VkCommandBuffer cmd, std::uint32_t frameIndex, const ImDrawData* data) {
    if (data == nullptr || data->TotalVtxCount == 0) {
        return;
    }
    const FrameData& frame = m_frames[frameIndex];
    const float framebufferWidth = data->DisplaySize.x * data->FramebufferScale.x;
    const float framebufferHeight = data->DisplaySize.y * data->FramebufferScale.y;
    if (framebufferWidth <= 0.0f || framebufferHeight <= 0.0f ||
        frame.vertices.buffer == VK_NULL_HANDLE || frame.indices.buffer == VK_NULL_HANDLE) {
        return;
    }
    bindState(cmd, frame, *data);

    std::uint32_t vertexBase = 0;
    std::uint32_t indexBase = 0;
    for (const ImDrawList* list : data->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                command.UserCallback(list, &command);
                bindState(cmd, frame, *data);
                continue;
            }
            const float minX = std::max(
                (command.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x, 0.0f);
            const float minY = std::max(
                (command.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y, 0.0f);
            const float maxX =
                std::min((command.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x,
                         framebufferWidth);
            const float maxY =
                std::min((command.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y,
                         framebufferHeight);
            if (maxX <= minX || maxY <= minY) {
                continue;
            }
            const VkRect2D scissor{
                {static_cast<std::int32_t>(minX), static_cast<std::int32_t>(minY)},
                {static_cast<std::uint32_t>(maxX - minX), static_cast<std::uint32_t>(maxY - minY)}};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            const auto descriptorSet =
                reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(command.GetTexID()));
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                                    &descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(cmd, command.ElemCount, 1, command.IdxOffset + indexBase,
                             static_cast<std::int32_t>(command.VtxOffset + vertexBase), 0);
        }
        vertexBase += static_cast<std::uint32_t>(list->VtxBuffer.Size);
        indexBase += static_cast<std::uint32_t>(list->IdxBuffer.Size);
    }
}

} // namespace ct
