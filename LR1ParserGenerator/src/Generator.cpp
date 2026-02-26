#include "Common/Graphviz.h"
#include "Common/Identifier.h"
#include "LR1ParserGenerator.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <queue>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace compiler::lr1 {
namespace {
struct LR1NormalizedGrammar {
    LR1CanonicalCollection collection;
    std::unordered_map<std::string, std::vector<std::size_t>>
        productions_by_lhs;
    std::unordered_set<std::string> terminal_set;
    std::unordered_set<std::string> nonterminal_set;
};

using SymbolSet = std::unordered_set<std::string>;
using SymbolSetMap = std::unordered_map<std::string, SymbolSet>;

bool ItemLess(const LR1Item &lhs, const LR1Item &rhs) {
    if (lhs.production_index != rhs.production_index) {
        return lhs.production_index < rhs.production_index;
    }
    if (lhs.dot != rhs.dot) {
        return lhs.dot < rhs.dot;
    }
    return lhs.lookahead < rhs.lookahead;
}

bool ItemEqual(const LR1Item &lhs, const LR1Item &rhs) {
    return lhs.production_index == rhs.production_index && lhs.dot == rhs.dot &&
           lhs.lookahead == rhs.lookahead;
}

std::vector<std::tuple<std::size_t, std::size_t, std::string>>
MakeItemKey(const std::vector<LR1Item> &items) {
    std::vector<std::tuple<std::size_t, std::size_t, std::string>> key;
    key.reserve(items.size());
    for (const auto &item : items) {
        key.emplace_back(item.production_index, item.dot, item.lookahead);
    }
    return key;
}

std::string
MakeAugmentedStartSymbol(const GrammarSpecAST &spec,
                         const std::unordered_set<std::string> &terminals,
                         const std::unordered_set<std::string> &nonterminals) {
    std::string base = "__LR1_AUG_START_" + spec.start_symbol;
    std::string candidate = base;
    std::size_t suffix = 0;
    while (terminals.contains(candidate) || nonterminals.contains(candidate)) {
        ++suffix;
        candidate = base + "_" + std::to_string(suffix);
    }
    return candidate;
}

const FlattenedProduction &
GetProductionChecked(const LR1CanonicalCollection &collection,
                     std::size_t index) {
    if (index >= collection.productions.size()) {
        throw BuildException("internal error: production index out of range");
    }
    return collection.productions[index];
}

std::vector<LR1Item> NormalizeItems(std::vector<LR1Item> items) {
    std::sort(items.begin(), items.end(), ItemLess);
    items.erase(std::unique(items.begin(), items.end(), ItemEqual),
                items.end());
    return items;
}

SymbolSet FirstOfTailThenLookahead(const std::vector<std::string> &rhs,
                                   std::size_t tail_start,
                                   std::string_view fallback_lookahead,
                                   const std::string &augmented_start_symbol,
                                   const SymbolSet &terminal_set,
                                   const SymbolSet &nonterminal_set,
                                   const SymbolSetMap &first_sets) {
    SymbolSet result;

    if (tail_start >= rhs.size()) {
        result.insert(std::string(fallback_lookahead));
        return result;
    }

    const std::string &symbol = rhs[tail_start];
    if (terminal_set.contains(symbol)) {
        result.insert(symbol);
        return result;
    }

    if (nonterminal_set.contains(symbol) || symbol == augmented_start_symbol) {
        const auto it = first_sets.find(symbol);
        if (it == first_sets.end()) {
            throw BuildException(
                "internal error: missing FIRST set for symbol " + symbol);
        }
        result.insert(it->second.begin(), it->second.end());
        return result;
    }

    throw BuildException(
        "internal error: unknown symbol in LR(1) lookahead computation: " +
        symbol);
}

std::vector<LR1Item>
Closure(const LR1CanonicalCollection &collection,
        const std::unordered_map<std::string, std::vector<std::size_t>>
            &productions_by_lhs,
        const SymbolSetMap &first_sets,
        const std::unordered_set<std::string> &terminal_set,
        const std::unordered_set<std::string> &nonterminal_set,
        std::vector<LR1Item> seed_items) {
    std::vector<LR1Item> items = NormalizeItems(std::move(seed_items));
    std::vector<LR1Item> worklist = items;

    for (std::size_t work_index = 0; work_index < worklist.size();
         ++work_index) {
        const LR1Item item = worklist[work_index];
        const auto &production =
            GetProductionChecked(collection, item.production_index);
        if (item.dot >= production.rhs.size()) {
            continue;
        }

        const std::string &symbol = production.rhs[item.dot];
        if (!nonterminal_set.contains(symbol)) {
            continue;
        }

        const auto prod_it = productions_by_lhs.find(symbol);
        if (prod_it == productions_by_lhs.end()) {
            continue;
        }

        const SymbolSet lookaheads = FirstOfTailThenLookahead(
            production.rhs, item.dot + 1, item.lookahead,
            collection.augmented_start_symbol, terminal_set, nonterminal_set,
            first_sets);
        for (std::size_t production_index : prod_it->second) {
            for (const auto &lookahead : lookaheads) {
                LR1Item new_item{production_index, 0, lookahead};
                auto existing = std::lower_bound(items.begin(), items.end(),
                                                 new_item, ItemLess);
                if (existing == items.end() ||
                    !ItemEqual(*existing, new_item)) {
                    items.insert(existing, new_item);
                    worklist.push_back(std::move(new_item));
                }
            }
        }
    }

    return items;
}

std::vector<LR1Item>
GotoItems(const LR1CanonicalCollection &collection,
          const std::unordered_map<std::string, std::vector<std::size_t>>
              &productions_by_lhs,
          const SymbolSetMap &first_sets,
          const std::unordered_set<std::string> &terminal_set,
          const std::unordered_set<std::string> &nonterminal_set,
          const std::vector<LR1Item> &state_items, std::string_view symbol) {
    std::vector<LR1Item> moved;
    for (const auto &item : state_items) {
        const auto &production =
            GetProductionChecked(collection, item.production_index);
        if (item.dot >= production.rhs.size()) {
            continue;
        }
        if (production.rhs[item.dot] == symbol) {
            moved.push_back(
                LR1Item{item.production_index, item.dot + 1, item.lookahead});
        }
    }

    if (moved.empty()) {
        return {};
    }

    return Closure(collection, productions_by_lhs, first_sets, terminal_set,
                   nonterminal_set, std::move(moved));
}

SymbolSetMap
ComputeFirstSetsNoEpsilon(const LR1CanonicalCollection &collection);

LR1NormalizedGrammar NormalizeGrammar(const GrammarSpecAST &spec) {
    if (spec.rules.empty()) {
        throw BuildException("grammar must contain at least one rule");
    }
    if (spec.start_symbol.empty()) {
        throw BuildException("grammar must define a start symbol");
    }

    LR1NormalizedGrammar normalized;

    for (const auto &terminal : spec.terminals) {
        if (terminal.empty()) {
            throw BuildException("terminal names must not be empty");
        }
        if (!normalized.terminal_set.insert(terminal).second) {
            throw BuildException("duplicate terminal: " + terminal);
        }
        normalized.collection.terminals.push_back(terminal);
    }

    for (const auto &rule : spec.rules) {
        if (rule.lhs.empty()) {
            throw BuildException("rule left-hand side must not be empty");
        }
        normalized.nonterminal_set.insert(rule.lhs);
    }

    {
        std::unordered_set<std::string> seen;
        for (const auto &rule : spec.rules) {
            if (seen.insert(rule.lhs).second) {
                normalized.collection.nonterminals.push_back(rule.lhs);
            }
        }
    }

    for (const auto &nonterminal : normalized.collection.nonterminals) {
        if (normalized.terminal_set.contains(nonterminal)) {
            throw BuildException("symbol declared as both token and rule: " +
                                 nonterminal);
        }
    }

    if (!normalized.nonterminal_set.contains(spec.start_symbol)) {
        throw BuildException("start symbol has no matching rule: " +
                             spec.start_symbol);
    }

    normalized.collection.augmented_start_symbol = MakeAugmentedStartSymbol(
        spec, normalized.terminal_set, normalized.nonterminal_set);

    normalized.collection.productions.push_back(FlattenedProduction{
        .lhs = normalized.collection.augmented_start_symbol,
        .rhs = {spec.start_symbol},
        .is_augmented = true,
        .source_rule_index = kInvalidIndex,
        .source_alternative_index = kInvalidIndex,
    });
    normalized.productions_by_lhs[normalized.collection.augmented_start_symbol]
        .push_back(0);

    for (std::size_t rule_index = 0; rule_index < spec.rules.size();
         ++rule_index) {
        const auto &rule = spec.rules[rule_index];
        for (std::size_t alt_index = 0; alt_index < rule.alternatives.size();
             ++alt_index) {
            const auto &alt = rule.alternatives[alt_index];
            if (alt.symbols.empty()) {
                throw BuildException("empty alternatives are not supported");
            }

            for (const auto &symbol : alt.symbols) {
                if (!normalized.terminal_set.contains(symbol) &&
                    !normalized.nonterminal_set.contains(symbol)) {
                    throw BuildException("undefined symbol in rule '" +
                                         rule.lhs + "': " + symbol);
                }
            }

            normalized.collection.productions.push_back(FlattenedProduction{
                .lhs = rule.lhs,
                .rhs = alt.symbols,
                .is_augmented = false,
                .source_rule_index = rule_index,
                .source_alternative_index = alt_index,
            });
            normalized.productions_by_lhs[rule.lhs].push_back(
                normalized.collection.productions.size() - 1);
        }
    }

    return normalized;
}

LR1CanonicalCollection
BuildCanonicalCollectionInternal(const GrammarSpecAST &spec) {
    LR1NormalizedGrammar normalized = NormalizeGrammar(spec);
    LR1CanonicalCollection &collection = normalized.collection;
    const SymbolSetMap first_sets = ComputeFirstSetsNoEpsilon(collection);

    using StateKey =
        std::vector<std::tuple<std::size_t, std::size_t, std::string>>;
    std::map<StateKey, std::size_t> state_index_by_key;
    std::queue<std::size_t> pending;

    const std::vector<LR1Item> start_items =
        Closure(collection, normalized.productions_by_lhs, first_sets,
                normalized.terminal_set, normalized.nonterminal_set,
                {LR1Item{0, 0, collection.end_marker}});

    collection.start_state = 0;
    collection.states.push_back(LR1State{start_items, {}});
    state_index_by_key.emplace(MakeItemKey(start_items), 0);
    pending.push(0);

    while (!pending.empty()) {
        const std::size_t state_index = pending.front();
        pending.pop();

        const auto items = collection.states[state_index].items;

        std::vector<std::string> next_symbols;
        {
            std::unordered_set<std::string> seen;
            for (const auto &item : items) {
                const auto &production =
                    GetProductionChecked(collection, item.production_index);
                if (item.dot >= production.rhs.size()) {
                    continue;
                }
                const std::string &symbol = production.rhs[item.dot];
                if (seen.insert(symbol).second) {
                    next_symbols.push_back(symbol);
                }
            }
            std::sort(next_symbols.begin(), next_symbols.end());
        }

        for (const auto &symbol : next_symbols) {
            const std::vector<LR1Item> next_items =
                GotoItems(collection, normalized.productions_by_lhs, first_sets,
                          normalized.terminal_set, normalized.nonterminal_set,
                          items, symbol);
            if (next_items.empty()) {
                continue;
            }

            const StateKey key = MakeItemKey(next_items);
            auto [it, inserted] =
                state_index_by_key.emplace(key, collection.states.size());
            if (inserted) {
                collection.states.push_back(LR1State{next_items, {}});
                pending.push(it->second);
            }

            collection.states[state_index].transitions.emplace_back(symbol,
                                                                    it->second);
        }
    }

    return collection;
}

bool ActionEquals(const LR1Action &lhs, const LR1Action &rhs) {
    return lhs.kind == rhs.kind && lhs.target_state == rhs.target_state &&
           lhs.production_index == rhs.production_index;
}

bool ActionLessForUnique(const LR1Action &lhs, const LR1Action &rhs) {
    if (lhs.kind != rhs.kind) {
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    }
    if (lhs.target_state != rhs.target_state) {
        return lhs.target_state < rhs.target_state;
    }
    return lhs.production_index < rhs.production_index;
}

LR1ConflictKind ClassifyConflict(const std::vector<LR1Action> &actions) {
    bool has_shift = false;
    std::size_t reduce_count = 0;

    for (const auto &action : actions) {
        if (action.kind == LR1ActionKind::Shift) {
            has_shift = true;
        } else if (action.kind == LR1ActionKind::Reduce) {
            ++reduce_count;
        }
    }

    if (has_shift && reduce_count > 0) {
        return LR1ConflictKind::ShiftReduce;
    }
    if (reduce_count >= 2) {
        return LR1ConflictKind::ReduceReduce;
    }
    return LR1ConflictKind::Other;
}

std::string ActionToString(const LR1Action &action) {
    switch (action.kind) {
    case LR1ActionKind::Shift:
        return "shift " + std::to_string(action.target_state);
    case LR1ActionKind::Reduce:
        return "reduce " + std::to_string(action.production_index);
    case LR1ActionKind::Accept:
        return "accept";
    }
    return "action?";
}

std::string ConflictKindToString(LR1ConflictKind kind) {
    switch (kind) {
    case LR1ConflictKind::ShiftReduce:
        return "shift/reduce";
    case LR1ConflictKind::ReduceReduce:
        return "reduce/reduce";
    case LR1ConflictKind::Other:
        return "other";
    }
    return "other";
}

std::string ItemToText(const LR1CanonicalCollection &collection,
                       const LR1Item &item) {
    const auto &production =
        GetProductionChecked(collection, item.production_index);
    std::ostringstream oss;
    oss << "[" << item.production_index << "] " << production.lhs << " -> ";
    for (std::size_t i = 0; i <= production.rhs.size(); ++i) {
        if (i == item.dot) {
            oss << ". ";
        }
        if (i < production.rhs.size()) {
            oss << production.rhs[i] << ' ';
        }
    }
    oss << ", " << item.lookahead;
    return oss.str();
}

std::string JoinLines(const std::vector<std::string> &lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out += "\n";
        }
        out += lines[i];
    }
    return out;
}

SymbolSetMap
ComputeFirstSetsNoEpsilon(const LR1CanonicalCollection &collection) {
    SymbolSetMap first_sets;
    const SymbolSet terminal_set(collection.terminals.begin(),
                                 collection.terminals.end());
    const SymbolSet nonterminal_set(collection.nonterminals.begin(),
                                    collection.nonterminals.end());

    for (const auto &nonterminal : collection.nonterminals) {
        first_sets.emplace(nonterminal, SymbolSet{});
    }
    first_sets.emplace(collection.augmented_start_symbol, SymbolSet{});

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &production : collection.productions) {
            if (production.rhs.empty()) {
                continue;
            }
            auto [lhs_it, _] =
                first_sets.try_emplace(production.lhs, SymbolSet{});
            SymbolSet &lhs_first = lhs_it->second;

            const std::string &first_symbol = production.rhs.front();
            if (terminal_set.contains(first_symbol)) {
                changed = lhs_first.insert(first_symbol).second || changed;
                continue;
            }

            if (nonterminal_set.contains(first_symbol) ||
                first_symbol == collection.augmented_start_symbol) {
                const auto rhs_it = first_sets.find(first_symbol);
                if (rhs_it == first_sets.end()) {
                    throw BuildException(
                        "internal error: missing FIRST set for nonterminal " +
                        first_symbol);
                }
                for (const auto &symbol : rhs_it->second) {
                    changed = lhs_first.insert(symbol).second || changed;
                }
                continue;
            }

            throw BuildException(
                "internal error: unknown symbol while computing FIRST sets: " +
                first_symbol);
        }
    }

    return first_sets;
}
} // namespace

BuildException::BuildException(std::string message)
    : std::runtime_error(std::move(message)) {}

LR1CanonicalCollection BuildLR1CanonicalCollection(const GrammarSpecAST &spec) {
    return BuildCanonicalCollectionInternal(spec);
}

LR1ParseTable BuildLR1ParseTable(const GrammarSpecAST &spec) {
    LR1ParseTable table;
    table.canonical_collection = BuildCanonicalCollectionInternal(spec);

    const auto &collection = table.canonical_collection;
    const std::size_t state_count = collection.states.size();

    std::vector<std::map<std::string, LR1Action>> action_maps(state_count);
    std::vector<std::map<std::string, std::size_t>> goto_maps(state_count);

    std::unordered_set<std::string> terminal_set(collection.terminals.begin(),
                                                 collection.terminals.end());
    std::unordered_set<std::string> nonterminal_set(
        collection.nonterminals.begin(), collection.nonterminals.end());
    if (terminal_set.contains(collection.end_marker)) {
        throw BuildException(
            "terminal set must not contain reserved end marker '" +
            collection.end_marker + "'");
    }

    std::map<std::pair<std::size_t, std::string>, std::size_t>
        conflict_index_by_key;

    auto add_or_update_conflict =
        [&](std::size_t state_index, const std::string &symbol,
            const LR1Action &existing, const LR1Action &incoming) {
            const auto key = std::make_pair(state_index, symbol);
            auto [it, inserted] =
                conflict_index_by_key.emplace(key, table.conflicts.size());
            if (inserted) {
                LR1Conflict conflict;
                conflict.state_index = state_index;
                conflict.symbol = symbol;
                conflict.actions = {existing, incoming};
                std::sort(conflict.actions.begin(), conflict.actions.end(),
                          ActionLessForUnique);
                conflict.actions.erase(std::unique(conflict.actions.begin(),
                                                   conflict.actions.end(),
                                                   ActionEquals),
                                       conflict.actions.end());
                conflict.kind = ClassifyConflict(conflict.actions);
                table.conflicts.push_back(std::move(conflict));
                return;
            }

            auto &conflict = table.conflicts[it->second];
            conflict.actions.push_back(existing);
            conflict.actions.push_back(incoming);
            std::sort(conflict.actions.begin(), conflict.actions.end(),
                      ActionLessForUnique);
            conflict.actions.erase(std::unique(conflict.actions.begin(),
                                               conflict.actions.end(),
                                               ActionEquals),
                                   conflict.actions.end());
            conflict.kind = ClassifyConflict(conflict.actions);
        };

    auto insert_action = [&](std::size_t state_index, const std::string &symbol,
                             const LR1Action &action) {
        auto &row = action_maps[state_index];
        auto [it, inserted] = row.emplace(symbol, action);
        if (inserted) {
            return;
        }
        if (ActionEquals(it->second, action)) {
            return;
        }
        add_or_update_conflict(state_index, symbol, it->second, action);
    };

    for (std::size_t state_index = 0; state_index < state_count;
         ++state_index) {
        const auto &state = collection.states[state_index];

        for (const auto &[symbol, target_state] : state.transitions) {
            if (terminal_set.contains(symbol)) {
                insert_action(state_index, symbol,
                              LR1Action{LR1ActionKind::Shift, target_state,
                                        kInvalidIndex});
            } else if (nonterminal_set.contains(symbol)) {
                goto_maps[state_index].emplace(symbol, target_state);
            } else {
                throw BuildException(
                    "internal error: transition symbol is neither "
                    "terminal nor nonterminal: " +
                    symbol);
            }
        }

        for (const auto &item : state.items) {
            const auto &production =
                GetProductionChecked(collection, item.production_index);
            if (item.dot < production.rhs.size()) {
                continue;
            }

            if (production.is_augmented) {
                if (item.lookahead != collection.end_marker) {
                    throw BuildException(
                        "internal error: augmented LR(1) item has "
                        "unexpected lookahead '" +
                        item.lookahead + "'");
                }
                insert_action(state_index, item.lookahead,
                              LR1Action{LR1ActionKind::Accept, kInvalidIndex,
                                        kInvalidIndex});
                continue;
            }

            if (!terminal_set.contains(item.lookahead) &&
                item.lookahead != collection.end_marker) {
                throw BuildException("internal error: LR(1) reduce item has "
                                     "invalid lookahead '" +
                                     item.lookahead + "'");
            }
            insert_action(state_index, item.lookahead,
                          LR1Action{LR1ActionKind::Reduce, kInvalidIndex,
                                    item.production_index});
        }
    }

    table.action_rows.resize(state_count);
    table.goto_rows.resize(state_count);
    for (std::size_t i = 0; i < state_count; ++i) {
        table.action_rows[i].assign(action_maps[i].begin(),
                                    action_maps[i].end());
        table.goto_rows[i].assign(goto_maps[i].begin(), goto_maps[i].end());
    }

    return table;
}

std::string ToDebugString(const LR1CanonicalCollection &collection) {
    std::ostringstream oss;
    oss << "LR1CanonicalCollection\n";
    oss << "  augmented_start_symbol: " << collection.augmented_start_symbol
        << "\n";
    oss << "  states: " << collection.states.size() << "\n";
    oss << "  productions: " << collection.productions.size() << "\n";
    for (std::size_t state_index = 0; state_index < collection.states.size();
         ++state_index) {
        const auto &state = collection.states[state_index];
        oss << "  state " << state_index << "\n";
        for (const auto &item : state.items) {
            oss << "    " << ItemToText(collection, item) << "\n";
        }
        for (const auto &[symbol, target] : state.transitions) {
            oss << "    on " << symbol << " -> " << target << "\n";
        }
    }
    return oss.str();
}

std::string ToDebugString(const LR1ParseTable &table) {
    std::ostringstream oss;
    oss << "LR1ParseTable\n";
    oss << "  states: " << table.canonical_collection.states.size() << "\n";
    oss << "  conflicts: " << table.conflicts.size() << "\n";
    for (std::size_t state_index = 0; state_index < table.action_rows.size();
         ++state_index) {
        oss << "  ACTION[" << state_index << "]\n";
        for (const auto &[symbol, action] : table.action_rows[state_index]) {
            oss << "    " << symbol << " = " << ActionToString(action) << "\n";
        }
        oss << "  GOTO[" << state_index << "]\n";
        for (const auto &[symbol, target] : table.goto_rows[state_index]) {
            oss << "    " << symbol << " = " << target << "\n";
        }
    }
    for (const auto &conflict : table.conflicts) {
        oss << "  conflict state " << conflict.state_index << " symbol "
            << conflict.symbol << " kind "
            << ConflictKindToString(conflict.kind) << ":";
        for (const auto &action : conflict.actions) {
            oss << " " << ActionToString(action);
        }
        oss << "\n";
    }
    return oss.str();
}

std::string
LR1CanonicalCollectionToGraphvizDot(const LR1CanonicalCollection &collection,
                                    std::string_view graph_name) {
    std::ostringstream oss;
    oss << "digraph "
        << compiler::common::SanitizeIdentifier(graph_name, "lr1_canonical_collection") << " {\n";
    oss << "  rankdir=LR;\n";
    oss << "  node [shape=box, fontname=\"monospace\"];\n";
    oss << "  __start [shape=point];\n";
    if (collection.start_state < collection.states.size()) {
        oss << "  __start -> s" << collection.start_state << ";\n";
    }

    for (std::size_t state_index = 0; state_index < collection.states.size();
         ++state_index) {
        const auto &state = collection.states[state_index];
        std::vector<std::string> lines;
        lines.push_back("I" + std::to_string(state_index));
        for (const auto &item : state.items) {
            lines.push_back(ItemToText(collection, item));
        }
        oss << "  s" << state_index << " [label=\""
            << compiler::common::EscapeGraphvizLabel(JoinLines(lines))
            << "\"];\n";
    }

    for (std::size_t state_index = 0; state_index < collection.states.size();
         ++state_index) {
        const auto &state = collection.states[state_index];
        for (const auto &[symbol, target] : state.transitions) {
            const bool is_terminal =
                std::find(collection.terminals.begin(),
                          collection.terminals.end(),
                          symbol) != collection.terminals.end();
            oss << "  s" << state_index << " -> s" << target << " [label=\""
                << compiler::common::EscapeGraphvizLabel(symbol) << "\"";
            if (is_terminal) {
                oss << ", color=\"royalblue\", fontcolor=\"royalblue\", "
                       "style=\"dashed\"";
            }
            oss << "];\n";
        }
    }

    oss << "}\n";
    return oss.str();
}

std::string LR1ParseTableToGraphvizDot(const LR1ParseTable &table,
                                       std::string_view graph_name) {
    std::ostringstream oss;
    oss << "digraph " << compiler::common::SanitizeIdentifier(graph_name, "lr1_parse_table")
        << " {\n";
    oss << "  rankdir=LR;\n";
    oss << "  node [shape=box, fontname=\"monospace\"];\n";
    oss << "  __start [shape=point];\n";
    if (table.canonical_collection.start_state <
        table.canonical_collection.states.size()) {
        oss << "  __start -> s" << table.canonical_collection.start_state
            << ";\n";
    }

    std::unordered_map<std::size_t, std::vector<const LR1Conflict *>>
        conflicts_by_state;
    for (const auto &conflict : table.conflicts) {
        conflicts_by_state[conflict.state_index].push_back(&conflict);
    }

    for (std::size_t state_index = 0;
         state_index < table.canonical_collection.states.size();
         ++state_index) {
        std::vector<std::string> lines;
        lines.push_back("state " + std::to_string(state_index));

        if (state_index < table.action_rows.size()) {
            for (const auto &[symbol, action] :
                 table.action_rows[state_index]) {
                lines.push_back("A[" + symbol +
                                "] = " + ActionToString(action));
            }
        }
        if (state_index < table.goto_rows.size()) {
            for (const auto &[symbol, target] : table.goto_rows[state_index]) {
                lines.push_back("G[" + symbol +
                                "] = " + std::to_string(target));
            }
        }
        const auto conflict_it = conflicts_by_state.find(state_index);
        if (conflict_it != conflicts_by_state.end()) {
            for (const LR1Conflict *conflict : conflict_it->second) {
                std::string line = "conflict " + conflict->symbol + " (" +
                                   ConflictKindToString(conflict->kind) + "):";
                for (const auto &action : conflict->actions) {
                    line += " " + ActionToString(action);
                }
                lines.push_back(std::move(line));
            }
        }

        oss << "  s" << state_index << " [label=\""
            << compiler::common::EscapeGraphvizLabel(JoinLines(lines)) << "\"";
        if (conflict_it != conflicts_by_state.end()) {
            oss << ", color=\"red\", penwidth=2";
        }
        oss << "];\n";
    }

    for (std::size_t state_index = 0; state_index < table.action_rows.size();
         ++state_index) {
        for (const auto &[symbol, action] : table.action_rows[state_index]) {
            if (action.kind != LR1ActionKind::Shift ||
                action.target_state == kInvalidIndex) {
                continue;
            }
            oss << "  s" << state_index << " -> s" << action.target_state
                << " [label=\"A:"
                << compiler::common::EscapeGraphvizLabel(symbol)
                << "\", color=\"royalblue\", fontcolor=\"royalblue\", "
                   "style=\"dashed\"];\n";
        }
    }
    for (std::size_t state_index = 0; state_index < table.goto_rows.size();
         ++state_index) {
        for (const auto &[symbol, target] : table.goto_rows[state_index]) {
            oss << "  s" << state_index << " -> s" << target << " [label=\"G:"
                << compiler::common::EscapeGraphvizLabel(symbol) << "\"];\n";
        }
    }

    oss << "}\n";
    return oss.str();
}
} // namespace compiler::lr1
