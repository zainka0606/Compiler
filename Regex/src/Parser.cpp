#include "Parser.h"

#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace compiler::regex {
namespace {
class Parser {
  public:
    explicit Parser(const std::string_view pattern) : pattern_(pattern) {}

    RegexNode ParsePattern() {
        RegexNode node = ParseExpression();
        if (!AtEnd()) {
            Error("unexpected character '" + DebugChar(Peek()) + "'");
        }
        return node;
    }

  private:
    std::string_view pattern_;
    std::size_t pos_ = 0;

    [[nodiscard]] bool AtEnd() const noexcept {
        return pos_ >= pattern_.size();
    }

    [[nodiscard]] char Peek() const { return AtEnd() ? '\0' : pattern_[pos_]; }

    [[nodiscard]] char PeekNext() const {
        return pos_ + 1 < pattern_.size() ? pattern_[pos_ + 1] : '\0';
    }

    char Advance() {
        if (AtEnd()) {
            Error("unexpected end of pattern");
        }
        return pattern_[pos_++];
    }

    bool Match(const char expected) {
        if (!AtEnd() && pattern_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void Expect(const char expected, std::string message) {
        if (!Match(expected)) {
            Error(std::move(message));
        }
    }

    [[noreturn]] void Error(const std::string &message) const {
        throw ParseException(pos_, message);
    }

    RegexNode ParseExpression() {
        std::vector<RegexNode> branches;
        branches.push_back(ParseConcatenation());

        while (Match('|')) {
            branches.push_back(ParseConcatenation());
        }

        if (branches.size() == 1) {
            return std::move(branches.front());
        }
        return RegexNode::Alternation(std::move(branches));
    }

    RegexNode ParseConcatenation() {
        std::vector<RegexNode> items;

        while (!AtEnd()) {
            const char c = Peek();
            if (c == ')' || c == '|') {
                break;
            }
            items.push_back(ParseRepetition());
        }

        if (items.empty()) {
            return RegexNode::Empty();
        }
        if (items.size() == 1) {
            return std::move(items.front());
        }
        return RegexNode::Sequence(std::move(items));
    }

    RegexNode ParseRepetition() {
        RegexNode atom = ParsePrimary();

        if (AtEnd()) {
            return atom;
        }

        if (IsQuantifierStart(Peek())) {
            const auto [min, max] = ParseQuantifier();
            atom = RegexNode::Repetition(std::move(atom), min, max);
            if (!AtEnd() && IsQuantifierStart(Peek())) {
                Error("multiple quantifiers applied to the same atom");
            }
        }

        return atom;
    }

    [[nodiscard]] static bool IsQuantifierStart(const char c) noexcept {
        return c == '*' || c == '+' || c == '?' || c == '{';
    }

    RegexNode ParsePrimary() {
        if (AtEnd()) {
            Error("unexpected end of pattern");
        }

        switch (Peek()) {
        case '(':
            return ParseGroup();
        case '.':
            Advance();
            return RegexNode::Dot();
        case '[':
            return ParseCharacterClass();
        case '\\':
            Advance();
            return RegexNode::Literal(
                ParseEscapedCharacter(/*in_character_class=*/false));
        case '*':
        case '+':
        case '?':
        case '{':
            Error("quantifier has no target");
        case ')':
            Error("unexpected ')'");
        case '|':
            Error("unexpected '|'");
        default: {
            const char c = Advance();
            return RegexNode::Literal(c);
        }
        }
    }

    RegexNode ParseGroup() {
        Expect('(', "expected '('");
        RegexNode expression = ParseExpression();
        Expect(')', "expected ')' to close group");
        return RegexNode::Group(std::move(expression));
    }

    RegexNode ParseCharacterClass() {
        Expect('[', "expected '['");
        const bool negated = Match('^');

        std::vector<CharacterClassItem> items;
        bool first_item = true;

        while (true) {
            if (AtEnd()) {
                Error("unterminated character class");
            }

            if (Peek() == ']' && !first_item) {
                Advance();
                break;
            }

            if (Peek() == ']' && first_item) {
                Error("empty character class is not allowed");
            }

            const char start = ParseClassCharacter();
            const bool can_start_range = start != '-';

            if (can_start_range && !AtEnd() && Peek() == '-' &&
                PeekNext() != ']' && PeekNext() != '\0') {
                Advance(); // consume '-'
                const char end = ParseClassCharacter();
                if (static_cast<unsigned char>(start) >
                    static_cast<unsigned char>(end)) {
                    Error("invalid character class range");
                }
                items.push_back(CharacterClassItem::Range(start, end));
            } else {
                items.push_back(CharacterClassItem::Character(start));
            }

            first_item = false;
        }

        return RegexNode::CharacterClass(negated, std::move(items));
    }

    char ParseClassCharacter() {
        if (AtEnd()) {
            Error("unterminated character class");
        }

        if (Peek() == '\\') {
            Advance();
            return ParseEscapedCharacter(/*in_character_class=*/true);
        }

        if (Peek() == ']') {
            Error("unexpected ']'");
        }

        return Advance();
    }

    char ParseEscapedCharacter(const bool in_character_class) {
        if (AtEnd()) {
            Error(in_character_class ? "incomplete escape in character class"
                                     : "incomplete escape sequence");
        }

        const char c = Advance();
        switch (c) {
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case 'r':
            return '\r';
        case '\\':
            return '\\';
        default:
            return c;
        }
    }

    std::pair<std::size_t, std::optional<std::size_t>> ParseQuantifier() {
        switch (Peek()) {
        case '*':
            Advance();
            return {0, std::nullopt};
        case '+':
            Advance();
            return {1, std::nullopt};
        case '?':
            Advance();
            return {0, 1};
        case '{':
            return ParseCountedQuantifier();
        default:
            Error("expected quantifier");
        }
    }

    std::pair<std::size_t, std::optional<std::size_t>>
    ParseCountedQuantifier() {
        Expect('{', "expected '{'");

        if (AtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
            Error("expected lower bound in counted quantifier");
        }

        const std::size_t min = ParseUnsignedNumber();
        std::optional max = min;

        if (Match(',')) {
            if (Match('}')) {
                return {min, std::nullopt};
            }

            if (AtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
                Error("expected upper bound in counted quantifier");
            }

            max = ParseUnsignedNumber();
            if (*max < min) {
                Error("counted quantifier upper bound is smaller than lower "
                      "bound");
            }
        }

        Expect('}', "expected '}' to close counted quantifier");
        return {min, max};
    }

    std::size_t ParseUnsignedNumber() {
        std::size_t value = 0;
        bool saw_digit = false;

        while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
            saw_digit = true;
            const unsigned digit = static_cast<unsigned>(Advance() - '0');
            if (value >
                (std::numeric_limits<std::size_t>::max() - digit) / 10) {
                Error("numeric value in quantifier is too large");
            }
            value = value * 10 + digit;
        }

        if (!saw_digit) {
            Error("expected number");
        }

        return value;
    }

    static std::string DebugChar(const char c) {
        switch (c) {
        case '\n':
            return "\\n";
        case '\t':
            return "\\t";
        case '\r':
            return "\\r";
        case '\'':
            return "\\'";
        case '\\':
            return "\\\\";
        default:
            return std::string(1, c);
        }
    }
};

std::string EscapeForDebug(const char c) {
    switch (c) {
    case '\n':
        return "\\n";
    case '\t':
        return "\\t";
    case '\r':
        return "\\r";
    case '\'':
        return "\\'";
    case '\\':
        return "\\\\";
    default:
        return std::string(1, c);
    }
}

void AppendDebug(const RegexNode &node, std::string &out) {
    auto append_children = [&](const std::vector<RegexNode> &children) {
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            AppendDebug(children[i], out);
        }
    };

    switch (node.type) {
    case RegexNode::Type::Empty:
        out += "empty";
        return;
    case RegexNode::Type::Literal:
        out += "lit('";
        out += EscapeForDebug(node.literal);
        out += "')";
        return;
    case RegexNode::Type::Dot:
        out += "dot";
        return;
    case RegexNode::Type::Sequence:
        out += "seq(";
        append_children(node.children);
        out += ")";
        return;
    case RegexNode::Type::Alternation:
        out += "alt(";
        append_children(node.children);
        out += ")";
        return;
    case RegexNode::Type::Repetition:
        out += "rep{";
        out += std::to_string(node.repetition.min);
        out += ",";
        out += node.repetition.max.has_value()
                   ? std::to_string(*node.repetition.max)
                   : "inf";
        out += "}(";
        if (!node.children.empty()) {
            AppendDebug(node.children.front(), out);
        }
        out += ")";
        return;
    case RegexNode::Type::Group:
        out += "group(";
        if (!node.children.empty()) {
            AppendDebug(node.children.front(), out);
        }
        out += ")";
        return;
    case RegexNode::Type::CharacterClass:
        out += node.char_class_negated ? "class^(" : "class(";
        for (std::size_t i = 0; i < node.char_class_items.size(); ++i) {
            if (i > 0) {
                out += ",";
            }

            const auto &item = node.char_class_items[i];
            if (item.is_range) {
                out += "range('";
                out += EscapeForDebug(item.first);
                out += "','";
                out += EscapeForDebug(item.last);
                out += "')";
            } else {
                out += "lit('";
                out += EscapeForDebug(item.first);
                out += "')";
            }
        }
        out += ")";
        return;
    }
}
} // namespace

CharacterClassItem CharacterClassItem::Character(const char c) {
    return CharacterClassItem{false, c, c};
}

CharacterClassItem CharacterClassItem::Range(const char first,
                                             const char last) {
    return CharacterClassItem{true, first, last};
}

RegexNode RegexNode::Empty() { return RegexNode{Type::Empty}; }

RegexNode RegexNode::Literal(const char c) {
    RegexNode node;
    node.type = Type::Literal;
    node.literal = c;
    return node;
}

RegexNode RegexNode::Dot() {
    RegexNode node;
    node.type = Type::Dot;
    return node;
}

RegexNode RegexNode::Sequence(std::vector<RegexNode> items) {
    RegexNode node;
    node.type = Type::Sequence;
    node.children = std::move(items);
    return node;
}

RegexNode RegexNode::Alternation(std::vector<RegexNode> items) {
    RegexNode node;
    node.type = Type::Alternation;
    node.children = std::move(items);
    return node;
}

RegexNode RegexNode::Repetition(RegexNode operand, const std::size_t min,
                                const std::optional<std::size_t> max) {
    RegexNode node;
    node.type = Type::Repetition;
    node.repetition = RepetitionBounds{min, max};
    node.children.push_back(std::move(operand));
    return node;
}

RegexNode RegexNode::Group(RegexNode expression) {
    RegexNode node;
    node.type = Type::Group;
    node.children.push_back(std::move(expression));
    return node;
}

RegexNode RegexNode::CharacterClass(const bool negated,
                                    std::vector<CharacterClassItem> items) {
    RegexNode node;
    node.type = Type::CharacterClass;
    node.char_class_negated = negated;
    node.char_class_items = std::move(items);
    return node;
}

ParseException::ParseException(const std::size_t position, std::string message)
    : std::runtime_error(std::move(message)), position_(position) {}

std::size_t ParseException::position() const noexcept { return position_; }

RegexNode Parse(const std::string_view pattern) {
    Parser parser(pattern);
    return parser.ParsePattern();
}

std::string ToDebugString(const RegexNode &node) {
    std::string result;
    AppendDebug(node, result);
    return result;
}
} // namespace compiler::regex