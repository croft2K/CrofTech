#pragma once

// clang-format off
#include <volk.h>
#include <vk_mem_alloc.h>
// clang-format on

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ImDrawData;
struct ImTextureData;

namespace ct {

// Draws Dear ImGui's output over the frame. Also serves ImGui's texture requests: its font atlas is
// created, grown and replaced at runtime as new glyphs and sizes are used.
class UiRenderer {
public:
    UiRenderer() = default;
    ~UiRenderer();
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    bool init(VkDevice device, VmaAllocator allocator, std::uint32_t framesInFlight,
              VkFormat colorFormat, VkFormat depthFormat,
              const std::filesystem::path& shaderDirectory, std::string& error);
    // Frees everything. The device must be idle. Textures ImGui still holds are marked destroyed,
    // so the ImGui context must still exist.
    void destroy();

    // Call after the frame's fence has signalled and before rendering begins: frees that frame's
    // old upload buffers, records texture uploads and copies the geometry. data may be null.
    void prepare(VkCommandBuffer cmd, std::uint32_t frame, const ImDrawData* data);
    // Records the draws inside the frame's rendering pass.
    void draw(VkCommandBuffer cmd, std::uint32_t frame, const ImDrawData* data);

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        ImTextureData* owner = nullptr;
    };
    struct FrameData {
        Buffer vertices;
        Buffer indices;
        std::vector<Buffer> uploads; // freed once the frame that used them has finished
    };

    bool createPipeline(VkFormat colorFormat, VkFormat depthFormat,
                        const std::filesystem::path& shaderDirectory, std::string& error);
    Buffer createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& buffer);
    bool ensureCapacity(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage);
    void updateTexture(VkCommandBuffer cmd, FrameData& frame, ImTextureData& data);
    Texture* createTexture(ImTextureData& data);
    void destroyTexture(Texture* texture);
    void bindState(VkCommandBuffer cmd, const FrameData& frame, const ImDrawData& data);

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = nullptr;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::vector<FrameData> m_frames;
    std::vector<std::unique_ptr<Texture>> m_textures;
};

} // namespace ct
