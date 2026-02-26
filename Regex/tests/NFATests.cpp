#include "NFA.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using compiler::regex::CompilePatternToNFA;
using compiler::regex::NFA;
using compiler::regex::NFAMatches;
using compiler::regex::ParseException;

void Fail(std::string message) {
    throw std::runtime_error(std::move(message));
}

void ExpectTrue(bool condition, std::string_view message) {
    if (!condition) {
        Fail(std::string(message));
    }
}

void ExpectMatch(std::string_view pattern, std::string_view input) {
    const NFA nfa = CompilePatternToNFA(pattern);
    if (!NFAMatches(nfa, input)) {
        std::ostringstream oss;
        oss << "expected match: pattern='" << pattern << "', input='" << input << "'";
        Fail(oss.str());
    }
}

void ExpectNoMatch(std::string_view pattern, std::string_view input) {
    const NFA nfa = CompilePatternToNFA(pattern);
    if (NFAMatches(nfa, input)) {
        std::ostringstream oss;
        oss << "expected no match: pattern='" << pattern << "', input='" << input << "'";
        Fail(oss.str());
    }
}

void ExpectInvalidPattern(std::string_view pattern) {
    try {
        (void)CompilePatternToNFA(pattern);
        Fail(std::string("expected parse failure for pattern '") + std::string(pattern) + "'");
    } catch (const ParseException&) {
        return;
    }
}

struct TestCase {
    const char* name;
    std::function<void()> run;
};

} // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"nfa_structure_is_valid", [] {
             const NFA nfa = CompilePatternToNFA("ab");
             ExpectTrue(!nfa.states.empty(), "NFA should contain states");
             ExpectTrue(nfa.start_state < nfa.states.size(), "start state out of range");
             ExpectTrue(nfa.accept_state < nfa.states.size(), "accept state out of range");
         }},
        {"empty_pattern", [] {
             ExpectMatch("", "");
             ExpectNoMatch("", "a");
         }},
        {"literal_concatenation", [] {
             ExpectMatch("abc", "abc");
             ExpectNoMatch("abc", "");
             ExpectNoMatch("abc", "ab");
             ExpectNoMatch("abc", "abcd");
         }},
        {"alternation_and_grouping", [] {
             ExpectMatch("a(b|c)d", "abd");
             ExpectMatch("a(b|c)d", "acd");
             ExpectNoMatch("a(b|c)d", "ad");
             ExpectNoMatch("a(b|c)d", "abcd");
         }},
        {"kleene_plus_optional", [] {
             ExpectMatch("a+b?", "a");
             ExpectMatch("a+b?", "aa");
             ExpectMatch("a+b?", "ab");
             ExpectMatch("a+b?", "aaab");
             ExpectNoMatch("a+b?", "");
             ExpectNoMatch("a+b?", "b");
             ExpectNoMatch("a+b?", "abb");
         }},
        {"counted_quantifiers", [] {
             ExpectMatch("x{2,4}", "xx");
             ExpectMatch("x{2,4}", "xxx");
             ExpectMatch("x{2,4}", "xxxx");
             ExpectNoMatch("x{2,4}", "x");
             ExpectNoMatch("x{2,4}", "xxxxx");
             ExpectMatch("a{0}", "");
             ExpectNoMatch("a{0}", "a");
             ExpectMatch("(ab){2,}", "abab");
             ExpectMatch("(ab){2,}", "ababab");
             ExpectNoMatch("(ab){2,}", "ab");
         }},
        {"dot_and_character_classes", [] {
             ExpectMatch("a.c", "abc");
             ExpectMatch("a.c", "a c");
             ExpectNoMatch("a.c", "ac");
             ExpectMatch("[a-cx]", "a");
             ExpectMatch("[a-cx]", "x");
             ExpectNoMatch("[a-cx]", "z");
             ExpectMatch("[^a-cx]", "z");
             ExpectNoMatch("[^a-cx]", "a");
         }},
        {"empty_alternative", [] {
             ExpectMatch("(a|)", "");
             ExpectMatch("(a|)", "a");
             ExpectNoMatch("(a|)", "aa");
         }},
        {"invalid_pattern_propagates_parse_error", [] {
             ExpectInvalidPattern("[");
         }},
    };

    std::size_t failures = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& ex) {
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
