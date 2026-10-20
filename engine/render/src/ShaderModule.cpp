#include "ShaderModule.h"

#include <cstdint>
#include <fstream>
#include <vector>

namespace ct {

VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& path,
                                std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamsize size = file ? static_cast<std::streamsize>(file.tellg()) : 0;
    std::vector<std::uint32_t> words;
    if (size > 0 && size % 4 == 0) {
        words.resize(static_cast<std::size_t>(size) / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), size);
    }
    if (words.empty() || !file) {
        error = "missing or unreadable shader " + path.string();
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = words.size() * sizeof(std::uint32_t),
        .pCode = words.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        error = "invalid shader " + path.string();
        return VK_NULL_HANDLE;
    }
    return module;
}

} // namespace ct
