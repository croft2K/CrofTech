#pragma once

#include <volk.h>

#include <filesystem>
#include <string>

namespace ct {

// Loads a compiled SPIR-V shader. Returns VK_NULL_HANDLE and sets error on failure.
VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& path,
                                std::string& error);

} // namespace ct
