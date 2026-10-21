#include "SaveFiles.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <fstream>
#include <system_error>
#include <vector>

namespace ct {
namespace {

constexpr std::string_view kExtension = ".save";
constexpr std::size_t kMaxNameLength = 64;

} // namespace

SaveFiles::SaveFiles(const std::string& gameName) {
    if (char* pref = SDL_GetPrefPath("CrofTech", gameName.c_str())) {
        m_directory = std::filesystem::path(reinterpret_cast<const char8_t*>(pref)) / "saves";
        SDL_free(pref);
    }
}

bool SaveFiles::isValidName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxNameLength &&
           std::all_of(name.begin(), name.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

bool SaveFiles::write(std::string_view name, const SaveGame& save, std::string& error) const {
    if (m_directory.empty()) {
        error = "there is no folder for saves";
        return false;
    }
    if (!isValidName(name)) {
        error = std::format("'{}' isn't a save name: use letters, digits, '_' and '-'", name);
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(m_directory, ec);
    const std::filesystem::path path = m_directory / (std::string(name) + std::string(kExtension));
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    const std::vector<std::byte> bytes = writeSaveGame(save);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            error = std::format("can't write {}", temporary.string());
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = std::format("can't replace {}: {}", path.string(), ec.message());
        return false;
    }
    return true;
}

std::optional<SaveGame> SaveFiles::read(std::string_view name, std::string& error) const {
    if (!isValidName(name)) {
        error = std::format("'{}' isn't a save name: use letters, digits, '_' and '-'", name);
        return std::nullopt;
    }
    const std::filesystem::path path = m_directory / (std::string(name) + std::string(kExtension));
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = std::format("there is no save called '{}'", name);
        return std::nullopt;
    }
    if (size > kMaxSaveFileBytes) {
        error = std::format("{} is too big to be a save", path.string());
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = std::format("can't read {}", path.string());
        return std::nullopt;
    }
    SaveGame save;
    if (!readSaveGame(bytes, save, error)) {
        error = std::format("{}: {}", path.filename().string(), error);
        return std::nullopt;
    }
    return save;
}

std::optional<std::string> SaveFiles::newest() const {
    std::optional<std::string> newest;
    std::filesystem::file_time_type newestTime;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_directory, ec)) {
        const std::filesystem::path& path = entry.path();
        const std::string stem = path.stem().string();
        if (path.extension() != kExtension || !isValidName(stem)) {
            continue;
        }
        const std::filesystem::file_time_type time = entry.last_write_time(ec);
        if (!ec && (!newest || time > newestTime)) {
            newest = stem;
            newestTime = time;
        }
    }
    return newest;
}

} // namespace ct
