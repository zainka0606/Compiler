#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace compiler::common {

namespace detail {

inline std::string_view TrimWhitespace(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

inline int DigitValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

inline double ParseUnsignedInteger(std::string_view digits, int base,
                                   std::string_view context) {
    std::uint64_t value = 0;
    bool saw_digit = false;
    for (const char c : digits) {
        if (c == '_') {
            continue;
        }
        const int digit = DigitValue(c);
        if (digit < 0 || digit >= base) {
            throw std::runtime_error("invalid numeric literal in " +
                                     std::string(context) + ": " +
                                     std::string(digits));
        }
        saw_digit = true;
        if (value > (std::numeric_limits<std::uint64_t>::max() -
                     static_cast<std::uint64_t>(digit)) /
                        static_cast<std::uint64_t>(base)) {
            throw std::runtime_error("numeric literal is out of range in " +
                                     std::string(context) + ": " +
                                     std::string(digits));
        }
        value = value * static_cast<std::uint64_t>(base) +
                static_cast<std::uint64_t>(digit);
    }
    if (!saw_digit) {
        throw std::runtime_error("invalid numeric literal in " +
                                 std::string(context) + ": " +
                                 std::string(digits));
    }
    return static_cast<double>(value);
}

} // namespace detail

inline double ParseNumericLiteral(std::string_view text,
                                  std::string_view context) {
    const std::string_view trimmed = detail::TrimWhitespace(text);
    if (trimmed.empty()) {
        throw std::runtime_error("empty numeric literal in " +
                                 std::string(context));
    }

    bool negative = false;
    std::size_t pos = 0;
    if (trimmed[pos] == '+' || trimmed[pos] == '-') {
        negative = trimmed[pos] == '-';
        ++pos;
    }
    if (pos >= trimmed.size()) {
        throw std::runtime_error("invalid numeric literal in " +
                                 std::string(context) + ": " +
                                 std::string(trimmed));
    }

    const std::string_view unsigned_text = trimmed.substr(pos);
    if (unsigned_text.size() > 2 && unsigned_text[0] == '0' &&
        (unsigned_text[1] == 'x' || unsigned_text[1] == 'X')) {
        const double value =
            detail::ParseUnsignedInteger(unsigned_text.substr(2), 16, context);
        return negative ? -value : value;
    }
    if (unsigned_text.size() > 2 && unsigned_text[0] == '0' &&
        (unsigned_text[1] == 'b' || unsigned_text[1] == 'B')) {
        const double value =
            detail::ParseUnsignedInteger(unsigned_text.substr(2), 2, context);
        return negative ? -value : value;
    }

    std::string copy(trimmed);
    copy.erase(std::remove(copy.begin(), copy.end(), '_'), copy.end());
    char *end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        throw std::runtime_error("invalid numeric literal in " +
                                 std::string(context) + ": " + copy);
    }
    return value;
}

} // namespace compiler::common
