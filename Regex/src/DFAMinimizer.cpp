#include "DFAMinimizer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace compiler::regex {
namespace {
inline constexpr std::size_t kNoState = static_cast<std::size_t>(-1);

struct NormalizedDFA {
    std::vector<std::array<std::size_t, 256>> transitions;
    std::vector<bool> accepting;
    std::size_t start_state = kNoState;
};

struct ReachabilityInfo {
    std::vector<std::size_t> reachable_order;
    std::vector<std::size_t> old_to_new;
};

struct DisjointSet {
    explicit DisjointSet(const std::size_t size) : parent(size), rank(size, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    std::size_t Find(const std::size_t x) {
        if (parent[x] != x) {
            parent[x] = Find(parent[x]);
        }
        return parent[x];
    }

    void Unite(std::size_t a, std::size_t b) {
        a = Find(a);
        b = Find(b);
        if (a == b) {
            return;
        }
        if (rank[a] < rank[b]) {
            std::swap(a, b);
        }
        parent[b] = a;
        if (rank[a] == rank[b]) {
            ++rank[a];
        }
    }

    std::vector<std::size_t> parent;
    std::vector<std::uint8_t> rank;
};

DFA MakeEmptyDFA() { return DFA{}; }

ReachabilityInfo ComputeReachableStates(const DFA &dfa) {
    ReachabilityInfo info;
    info.old_to_new.assign(dfa.states.size(), kNoState);

    if (dfa.start_state == kInvalidDFAState ||
        dfa.start_state >= dfa.states.size()) {
        return info;
    }

    std::queue<std::size_t> pending;
    pending.push(dfa.start_state);

    while (!pending.empty()) {
        const std::size_t state = pending.front();
        pending.pop();

        if (state >= dfa.states.size() || info.old_to_new[state] != kNoState) {
            continue;
        }

        const std::size_t new_index = info.reachable_order.size();
        info.old_to_new[state] = new_index;
        info.reachable_order.push_back(state);

        for (std::size_t target : dfa.states[state].transitions) {
            if (target != kInvalidDFAState && target < dfa.states.size() &&
                info.old_to_new[target] == kNoState) {
                pending.push(target);
            }
        }
    }

    return info;
}

NormalizedDFA NormalizeReachableDFA(const DFA &dfa) {
    const ReachabilityInfo reachable = ComputeReachableStates(dfa);
    if (reachable.reachable_order.empty()) {
        return {};
    }

    bool needs_sink = false;
    for (std::size_t old_state : reachable.reachable_order) {
        for (std::size_t target : dfa.states[old_state].transitions) {
            if (target == kInvalidDFAState || target >= dfa.states.size() ||
                reachable.old_to_new[target] == kNoState) {
                needs_sink = true;
                break;
            }
        }
        if (needs_sink) {
            break;
        }
    }

    NormalizedDFA normalized;
    normalized.start_state = 0;
    normalized.transitions.resize(reachable.reachable_order.size());
    normalized.accepting.resize(reachable.reachable_order.size(), false);

    const std::size_t sink_state =
        needs_sink ? normalized.transitions.size() : kNoState;
    if (needs_sink) {
        normalized.transitions.push_back({});
        normalized.accepting.push_back(false);
        normalized.transitions.back().fill(sink_state);
    }

    for (std::size_t new_index = 0;
         new_index < reachable.reachable_order.size(); ++new_index) {
        const std::size_t old_index = reachable.reachable_order[new_index];
        const auto &old_state = dfa.states[old_index];

        normalized.accepting[new_index] = old_state.is_accepting;

        auto &transitions = normalized.transitions[new_index];
        for (std::size_t byte_value = 0; byte_value < transitions.size();
             ++byte_value) {
            const std::size_t target = old_state.transitions[byte_value];
            if (target == kInvalidDFAState || target >= dfa.states.size()) {
                transitions[byte_value] = sink_state;
                continue;
            }

            const std::size_t remapped = reachable.old_to_new[target];
            transitions[byte_value] =
                remapped == kNoState ? sink_state : remapped;
        }
    }

    return normalized;
}

bool IsMarked(const std::vector<std::uint8_t> &marked, const std::size_t n,
              std::size_t a, std::size_t b) {
    if (a == b) {
        return false;
    }
    if (a > b) {
        std::swap(a, b);
    }
    return marked[a * n + b] != 0;
}

void Mark(std::vector<std::uint8_t> &marked, const std::size_t n, std::size_t a,
          std::size_t b) {
    if (a == b) {
        return;
    }
    if (a > b) {
        std::swap(a, b);
    }
    marked[a * n + b] = 1;
}

DFA MinimizeNormalizedDFA(const NormalizedDFA &normalized) {
    const std::size_t n = normalized.transitions.size();
    if (n == 0 || normalized.start_state == kNoState ||
        normalized.start_state >= n) {
        return MakeEmptyDFA();
    }

    std::vector<std::uint8_t> marked(n * n, 0);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (normalized.accepting[i] != normalized.accepting[j]) {
                Mark(marked, n, i, j);
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                if (IsMarked(marked, n, i, j)) {
                    continue;
                }

                for (std::size_t byte_value = 0; byte_value < 256;
                     ++byte_value) {
                    const std::size_t next_i =
                        normalized.transitions[i][byte_value];
                    const std::size_t next_j =
                        normalized.transitions[j][byte_value];
                    if (next_i == next_j) {
                        continue;
                    }
                    if (IsMarked(marked, n, next_i, next_j)) {
                        Mark(marked, n, i, j);
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    DisjointSet dsu(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!IsMarked(marked, n, i, j)) {
                dsu.Unite(i, j);
            }
        }
    }

    std::unordered_map<std::size_t, std::size_t> root_to_class;
    root_to_class.reserve(n);
    std::vector state_to_class(n, kNoState);

    for (std::size_t state = 0; state < n; ++state) {
        const std::size_t root = dsu.Find(state);
        auto [it, inserted] = root_to_class.emplace(root, root_to_class.size());
        (void)inserted;
        state_to_class[state] = it->second;
    }

    DFA minimized;
    minimized.states.resize(root_to_class.size());
    minimized.start_state = state_to_class[normalized.start_state];

    for (auto &state : minimized.states) {
        state.transitions.fill(kInvalidDFAState);
        state.is_accepting = false;
    }

    std::vector representative_for_class(minimized.states.size(),
                                                      kNoState);
    for (std::size_t state = 0; state < n; ++state) {
        const std::size_t class_index = state_to_class[state];
        if (representative_for_class[class_index] == kNoState) {
            representative_for_class[class_index] = state;
        }
        if (normalized.accepting[state]) {
            minimized.states[class_index].is_accepting = true;
        }
    }

    for (std::size_t class_index = 0; class_index < minimized.states.size();
         ++class_index) {
        const std::size_t representative =
            representative_for_class[class_index];
        auto &out_state = minimized.states[class_index];

        for (std::size_t byte_value = 0; byte_value < 256; ++byte_value) {
            const std::size_t next_state =
                normalized.transitions[representative][byte_value];
            out_state.transitions[byte_value] = state_to_class[next_state];
        }
    }

    return minimized;
}
} // namespace

DFA MinimizeDFA(const DFA &dfa) {
    return MinimizeNormalizedDFA(NormalizeReachableDFA(dfa));
}

DFA CompilePatternToMinimizedDFA(const std::string_view pattern) {
    return MinimizeDFA(CompilePatternToDFA(pattern));
}
} // namespace compiler::regex