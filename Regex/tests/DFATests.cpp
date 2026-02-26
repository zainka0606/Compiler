#include "DFA.h"

#include "NFA.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using compiler::regex::CompilePatternToDFA;
using compiler::regex::CompilePatternToNFA;
using compiler::regex::DFA;
using compiler::regex::DFAMatches;
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

void ExpectDFAMatch(std::string_view pattern, std::string_view input) {
    const DFA dfa = CompilePatternToDFA(pattern);
    if (!DFAMatches(dfa, input)) {
        std::ostringstream oss;
        oss << "expected DFA match: pattern='" << pattern << "', input='" << input << "'";
        Fail(oss.str());
    }
}

void ExpectDFANoMatch(std::string_view pattern, std::string_view input) {
    const DFA dfa = CompilePatternToDFA(pattern);
    if (DFAMatches(dfa, input)) {
        std::ostringstream oss;
        oss << "expected DFA no match: pattern='" << pattern << "', input='" << input << "'";
        Fail(oss.str());
    }
}

void ExpectInvalidPattern(std::string_view pattern) {
    try {
        (void)CompilePatternToDFA(pattern);
        Fail(std::string("expected parse failure for pattern '") + std::string(pattern) + "'");
    } catch (const ParseException&) {
        return;
    }
}

void ExpectNFADFAParity(std::string_view pattern, const std::vector<std::string_view>& inputs) {
    const NFA nfa = CompilePatternToNFA(pattern);
    const DFA dfa = CompilePatternToDFA(pattern);

    for (std::string_view input : inputs) {
        const bool nfa_match = NFAMatches(nfa, input);
        const bool dfa_match = DFAMatches(dfa, input);
        if (nfa_match != dfa_match) {
            std::ostringstream oss;
            oss << "NFA/DFA mismatch for pattern='" << pattern << "', input='" << input
                << "' (nfa=" << nfa_match << ", dfa=" << dfa_match << ")";
            Fail(oss.str());
        }
    }
}

struct TestCase {
    const char* name;
    std::function<void()> run;
};

} // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"dfa_structure_is_valid", [] {
             const DFA dfa = CompilePatternToDFA("ab");
             ExpectTrue(!dfa.states.empty(), "DFA should contain states");
             ExpectTrue(dfa.start_state < dfa.states.size(), "start state out of range");
         }},
        {"empty_pattern", [] {
             ExpectDFAMatch("", "");
             ExpectDFANoMatch("", "a");
         }},
        {"literal_and_alternation", [] {
             ExpectDFAMatch("cat|dog", "cat");
             ExpectDFAMatch("cat|dog", "dog");
             ExpectDFANoMatch("cat|dog", "do");
             ExpectDFANoMatch("cat|dog", "cats");
         }},
        {"grouping_and_quantifiers", [] {
             ExpectDFAMatch("a(b|c)*d", "ad");
             ExpectDFAMatch("a(b|c)*d", "abcd");
             ExpectDFAMatch("a(b|c)*d", "abcbcd");
             ExpectDFANoMatch("a(b|c)*d", "abce");
             ExpectDFANoMatch("a(b|c)*d", "a");
         }},
        {"counted_quantifiers", [] {
             ExpectDFAMatch("x{2,4}", "xx");
             ExpectDFAMatch("x{2,4}", "xxxx");
             ExpectDFANoMatch("x{2,4}", "x");
             ExpectDFANoMatch("x{2,4}", "xxxxx");
             ExpectDFAMatch("(ab){2,}", "abab");
             ExpectDFANoMatch("(ab){2,}", "ab");
         }},
        {"dot_and_character_classes", [] {
             ExpectDFAMatch("a.c", "abc");
             ExpectDFANoMatch("a.c", "ac");
             ExpectDFAMatch("[^a-cx]", "z");
             ExpectDFANoMatch("[^a-cx]", "a");
             ExpectDFAMatch("[a-cx]{2}", "bx");
             ExpectDFANoMatch("[a-cx]{2}", "bz");
         }},
        {"nfa_dfa_parity_suite", [] {
             ExpectNFADFAParity("", {"", "a"});
             ExpectNFADFAParity("ab|cd", {"", "ab", "cd", "ad", "abcd"});
             ExpectNFADFAParity("a+b?", {"", "a", "aa", "ab", "aaab", "b", "abb"});
             ExpectNFADFAParity("(a|)", {"", "a", "aa"});
             ExpectNFADFAParity("[a-c]*d", {"d", "ad", "abcd", "zzd", ""});
             ExpectNFADFAParity(".{2,3}", {"", "a", "ab", "abc", "abcd"});
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
