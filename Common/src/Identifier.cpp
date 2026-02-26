#include "Common/Identifier.h"

#include <cctype>

namespace compiler::common {
std::string SanitizeIdentifier(std::string_view text,
                               std::string_view fallback) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = std::string(fallback);
    }
    if (std::isdigit(static_cast<unsigned char>(out.front()))) {
        out.insert(out.begin(), '_');
    }
    return out;
}
} // namespace compiler::common