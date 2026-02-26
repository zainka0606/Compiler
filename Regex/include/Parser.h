#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace compiler::regex {

struct CharacterClassItem {
    bool is_range = false;
    char first = '\0';
    char last = '\0';

    static CharacterClassItem Character(char c);
    static CharacterClassItem Range(char first, char last);
};

struct RegexNode {
    enum class Type {
        Empty,
        Literal,
        Dot,
        Sequence,
        Alternation,
        Repetition,
        Group,
        CharacterClass
    };

    struct RepetitionBounds {
        std::size_t min = 0;
        std::optional<std::size_t> max{};
    };

    Type type = Type::Empty;
    char literal = '\0';
    bool char_class_negated = false;
    std::vector<CharacterClassItem> char_class_items;
    RepetitionBounds repetition{};
    std::vector<RegexNode> children;

    static RegexNode Empty();
    static RegexNode Literal(char c);
    static RegexNode Dot();
    static RegexNode Sequence(std::vector<RegexNode> items);
    static RegexNode Alternation(std::vector<RegexNode> items);
    static RegexNode Repetition(RegexNode operand, std::size_t min, std::optional<std::size_t> max);
    static RegexNode Group(RegexNode expression);
    static RegexNode CharacterClass(bool negated, std::vector<CharacterClassItem> items);
};

class ParseException : public std::runtime_error {
public:
    ParseException(std::size_t position, std::string message);

    [[nodiscard]] std::size_t position() const noexcept;

private:
    std::size_t position_;
};

RegexNode Parse(std::string_view pattern);
std::string ToDebugString(const RegexNode& node);

} // namespace compiler::regex
