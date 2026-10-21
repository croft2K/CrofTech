// Runs a fuzz target over files instead of under libFuzzer, so ordinary builds check that every
// seed in the corpus is handled. Each argument is a file or a directory of files.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> files;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path path(argv[i]);
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path());
                }
            }
        } else {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "no inputs\n");
        return 1;
    }
    for (const std::filesystem::path& path : files) {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes(std::istreambuf_iterator<char>(file), {});
        LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    }
    std::printf("ran %zu inputs\n", files.size());
    return 0;
}
