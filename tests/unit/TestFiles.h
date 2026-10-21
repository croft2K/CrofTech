#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

namespace ct::test {

// A fresh directory under the system temp folder, removed afterwards.
class TempDirectory {
public:
    TempDirectory() {
        std::random_device random;
        m_path =
            std::filesystem::temp_directory_path() / ("croftech-test-" + std::to_string(random()));
        std::filesystem::create_directories(m_path);
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

inline void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

} // namespace ct::test
