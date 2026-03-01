#include "DFAMinimizer.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using compiler::regex::CompilePatternToDFA;
using compiler::regex::CompilePatternToMinimizedDFA;
using compiler::regex::DFA;
using compiler::regex::DFAMatches;
using compiler::regex::kInvalidDFAState;
using compiler::regex::MinimizeDFA;
using compiler::regex::ParseException;

void Fail(std::string message) { throw std::runtime_error(std::move(message)); }

void ExpectTrue(const bool condition, const std::string_view message) {
    if (!condition) {
        Fail(std::string(message));
    }
}

void ExpectEqual(const std::size_t actual, const std::size_t expected,
                 const std::string_view message) {
    if (actual == expected) {
        return;
    }
    std::ostringstream oss;
    oss << message << " (expected=" << expected << ", actual=" << actual << ")";
    Fail(oss.str());
}

void ExpectSameLanguage(const DFA &lhs, const DFA &rhs,
                        const std::vector<std::string_view> &inputs) {
    for (std::string_view input : inputs) {
        const bool lhs_match = DFAMatches(lhs, input);
        const bool rhs_match = DFAMatches(rhs, input);
        if (lhs_match != rhs_match) {
            std::ostringstream oss;
            oss << "language mismatch on input '" << input
                << "' (lhs=" << lhs_match << ", rhs=" << rhs_match << ")";
            Fail(oss.str());
        }
    }
}

compiler::regex::DFAState MakeState(const std::size_t default_target,
                                    const bool accepting = false) {
    compiler::regex::DFAState state;
    state.transitions.fill(default_target);
    state.is_accepting = accepting;
    return state;
}

DFA MakeReducibleManualDFA() {
    // Language: exactly "ax" or "bx". States 1 and 2 are equivalent and should
    // merge.
    DFA dfa;
    dfa.states.resize(6);
    dfa.start_state = 0;

    constexpr std::size_t dead = 4;
    constexpr std::size_t unreachable = 5;

    dfa.states[0] = MakeState(dead, false);
    dfa.states[1] = MakeState(dead, false);
    dfa.states[2] = MakeState(dead, false);
    dfa.states[3] = MakeState(dead, true);
    dfa.states[4] = MakeState(dead, false);
    dfa.states[5] = MakeState(unreachable, false); // Unreachable self-loop.

    dfa.states[0].transitions[static_cast<unsigned char>('a')] = 1;
    dfa.states[0].transitions[static_cast<unsigned char>('b')] = 2;
    dfa.states[1].transitions[static_cast<unsigned char>('x')] = 3;
    dfa.states[2].transitions[static_cast<unsigned char>('x')] = 3;

    return dfa;
}

void ExpectInvalidPattern(const std::string_view pattern) {
    try {
        (void)CompilePatternToMinimizedDFA(pattern);
        Fail(std::string("expected parse failure for pattern '") +
             std::string(pattern) + "'");
    } catch (const ParseException &) {
        return;
    }
}

struct TestCase {
    const char *name;
    std::function<void()> run;
};
} // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"manual_dfa_is_reduced_and_unreachable_removed",
         [] {
             const DFA original = MakeReducibleManualDFA();
             const DFA minimized = MinimizeDFA(original);

             ExpectEqual(original.states.size(), 6,
                         "sanity check original state count");
             ExpectEqual(minimized.states.size(), 4, "minimized state count");
             ExpectTrue(minimized.start_state < minimized.states.size(),
                        "start state out of range");
         }},
        {"manual_dfa_language_preserved",
         [] {
             const DFA original = MakeReducibleManualDFA();
             const DFA minimized = MinimizeDFA(original);
             ExpectSameLanguage(
                 original, minimized,
                 {"", "a", "b", "x", "ax", "bx", "abx", "axx", "cx"});
         }},
        {"handles_incomplete_dfa_by_normalizing_sink",
         [] {
             DFA dfa;
             dfa.states.resize(2);
             dfa.start_state = 0;
             for (auto &state : dfa.states) {
                 state.transitions.fill(kInvalidDFAState);
             }
             dfa.states[1].is_accepting = true;
             dfa.states[0].transitions[static_cast<unsigned char>('z')] = 1;

             const DFA minimized = MinimizeDFA(dfa);
             ExpectSameLanguage(dfa, minimized, {"", "z", "zz", "a"});
         }},
        {"regex_compiled_dfa_minimization_preserves_language",
         [] {
             const std::vector<std::string_view> patterns = {
                 "", "ab|cd", "a(b|c)*d", "x{2,4}", "[a-c]*d", ".{2,3}"};

             const std::vector<std::string_view> inputs = {
                 "",     "a", "ab",   "cd",  "ad",  "abcd", "xx",
                 "xxxx", "d", "aaad", "zzd", "abz", "abc",  "abcd"};

             for (std::string_view pattern : patterns) {
                 const DFA dfa = CompilePatternToDFA(pattern);
                 const DFA minimized = MinimizeDFA(dfa);
                 ExpectSameLanguage(dfa, minimized, inputs);
             }
         }},
        {"compile_pattern_to_minimized_dfa",
         [] {
             const DFA dfa = CompilePatternToMinimizedDFA("ac|bc");
             ExpectTrue(!dfa.states.empty(),
                        "compiled minimized DFA should not be empty");
             ExpectTrue(DFAMatches(dfa, "ac"), "expected match for ac");
             ExpectTrue(DFAMatches(dfa, "bc"), "expected match for bc");
             ExpectTrue(!DFAMatches(dfa, "abc"), "expected no match for abc");
         }},
        {"invalid_pattern_propagates_parse_error",
         [] { ExpectInvalidPattern("("); }},
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