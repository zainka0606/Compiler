#include "Common/Graphviz.h"

namespace compiler::common {
std::string EscapeGraphvizLabel(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (char c : text) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}
} // namespace compiler::common
