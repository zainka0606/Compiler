#include "Common/FileIO.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace compiler::common {
std::string ReadTextFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        throw std::runtime_error("failed to read file: " + path.string());
    }
    return buffer.str();
}

void WriteTextFile(const std::filesystem::path &path, std::string_view text) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open output file: " +
                                 path.string());
    }

    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        throw std::runtime_error("failed to write output file: " +
                                 path.string());
    }
}
} // namespace compiler::common
