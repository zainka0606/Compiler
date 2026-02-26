#include "Parser.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using compiler::regex::Parse;
using compiler::regex::ParseException;
using compiler::regex::ToDebugString;

void ExpectEqual(std::string_view actual, std::string_view expected,
                 std::string_view context) {
    if (actual == expected) {
        return;
    }

    std::ostringstream oss;
    oss << context << "\nexpected: " << expected << "\nactual:   " << actual;
    throw std::runtime_error(oss.str());
}

void ExpectTrue(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void ExpectParsed(std::string_view pattern, std::string_view expected_debug) {
    const auto ast = Parse(pattern);
    const auto debug = ToDebugString(ast);
    ExpectEqual(debug, expected_debug,
                std::string("parse(") + std::string(pattern) + ")");
}

void ExpectParseError(std::string_view pattern, std::size_t expected_position,
                      std::string_view expected_message_substring) {
    try {
        (void)Parse(pattern);
        throw std::runtime_error(
            std::string("expected parse error for pattern: ") +
            std::string(pattern));
    } catch (const ParseException &ex) {
        if (ex.position() != expected_position) {
            std::ostringstream oss;
            oss << "wrong error position for pattern '" << pattern
                << "': expected " << expected_position << ", got "
                << ex.position();
            throw std::runtime_error(oss.str());
        }
        ExpectTrue(std::string_view(ex.what()).find(
                       expected_message_substring) != std::string_view::npos,
                   std::string("error message did not contain '") +
                       std::string(expected_message_substring) + "'");
    }
}

struct TestCase {
    const char *name;
    std::function<void()> run;
};
} // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"concatenation",
         [] { ExpectParsed("abc", "seq(lit('a'),lit('b'),lit('c'))"); }},
        {"alternation_precedence",
         [] {
             ExpectParsed("ab|cd",
                          "alt(seq(lit('a'),lit('b')),seq(lit('c'),lit('d')))");
         }},
        {"grouping",
         [] {
             ExpectParsed(
                 "a(b|c)d",
                 "seq(lit('a'),group(alt(lit('b'),lit('c'))),lit('d'))");
         }},
        {"quantifiers",
         [] {
             ExpectParsed(
                 "a*b+c?d{2,4}e{3}f{1,}",
                 "seq(rep{0,inf}(lit('a')),rep{1,inf}(lit('b')),rep{0,1}(lit('"
                 "c')),"
                 "rep{2,4}(lit('d')),rep{3,3}(lit('e')),rep{1,inf}(lit('f')))");
         }},
        {"dot_and_escape", [] { ExpectParsed(".\\.", "seq(dot,lit('.'))"); }},
        {"character_class",
         [] { ExpectParsed("[^a-z\\]]", "class^(range('a','z'),lit(']'))"); }},
        {"empty_alternative",
         [] { ExpectParsed("(a|)", "group(alt(lit('a'),empty))"); }},
        {"error_unclosed_group",
         [] { ExpectParseError("(", 1, "expected ')'"); }},
        {"error_leading_quantifier",
         [] { ExpectParseError("*a", 0, "quantifier has no target"); }},
        {"error_duplicate_quantifier",
         [] { ExpectParseError("a**", 2, "multiple quantifiers"); }},
        {"error_bad_range",
         [] { ExpectParseError("[z-a]", 4, "invalid character class range"); }},
        {"error_bad_counted_quantifier",
         [] { ExpectParseError("a{3,2}", 5, "smaller than lower bound"); }},
    };

    std::size_t failures = 0;

    for (const auto &test : tests) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception &ex) {
            ++failures;
            std::cout << "[FAIL] " << test.name << ": " << ex.what() << '\n';
        }
    }

    if (failures > 0) {
        std::cout << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << tests.size() << " test(s) passed.\n";
    return 0;
}