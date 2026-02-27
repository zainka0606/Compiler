#pragma once

#include "LR1ParserGenerator.h"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace compiler::parsergen1 {

struct GenericToken {
    std::string kind;
    std::string lexeme;
    std::size_t line = 1;
    std::size_t column = 1;
};

class CSTNode {
  public:
    virtual ~CSTNode() = default;

    [[nodiscard]] virtual std::string_view Symbol() const = 0;
    [[nodiscard]] virtual bool IsTerminal() const = 0;
    [[nodiscard]] virtual std::string_view Lexeme() const;
    [[nodiscard]] virtual std::size_t Line() const = 0;
    [[nodiscard]] virtual std::size_t Column() const = 0;
    [[nodiscard]] virtual const std::vector<std::unique_ptr<CSTNode>> &
    Children() const;
    [[nodiscard]] virtual std::size_t ProductionIndex() const;
    [[nodiscard]] virtual std::size_t SourceRuleIndex() const;
    [[nodiscard]] virtual std::size_t SourceAlternativeIndex() const;

    [[nodiscard]] std::size_t ChildCount() const;
    [[nodiscard]] const CSTNode &Child(std::size_t index) const;
};

class CSTTerminalNode final : public CSTNode {
  public:
    CSTTerminalNode(std::string symbol, std::string lexeme, std::size_t line,
                    std::size_t column);

    [[nodiscard]] std::string_view Symbol() const override;
    [[nodiscard]] bool IsTerminal() const override;
    [[nodiscard]] std::string_view Lexeme() const override;
    [[nodiscard]] std::size_t Line() const override;
    [[nodiscard]] std::size_t Column() const override;

  private:
    std::string symbol_;
    std::string lexeme_;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

class CSTNonterminalNode final : public CSTNode {
  public:
    CSTNonterminalNode(std::string symbol, std::size_t line, std::size_t column,
                       std::vector<std::unique_ptr<CSTNode>> children,
                       std::size_t production_index,
                       std::size_t source_rule_index,
                       std::size_t source_alternative_index);

    [[nodiscard]] std::string_view Symbol() const override;
    [[nodiscard]] bool IsTerminal() const override;
    [[nodiscard]] std::size_t Line() const override;
    [[nodiscard]] std::size_t Column() const override;
    [[nodiscard]] const std::vector<std::unique_ptr<CSTNode>> &
    Children() const override;
    [[nodiscard]] std::size_t ProductionIndex() const override;
    [[nodiscard]] std::size_t SourceRuleIndex() const override;
    [[nodiscard]] std::size_t SourceAlternativeIndex() const override;

  private:
    std::string symbol_;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
    std::vector<std::unique_ptr<CSTNode>> children_;
    std::size_t production_index_ = compiler::lr1::kInvalidIndex;
    std::size_t source_rule_index_ = compiler::lr1::kInvalidIndex;
    std::size_t source_alternative_index_ = compiler::lr1::kInvalidIndex;
};

struct CST {
    std::unique_ptr<CSTNode> root;

    CST() = default;
    CST(CST &&) noexcept = default;
    CST &operator=(CST &&) noexcept = default;
    CST(const CST &) = delete;
    CST &operator=(const CST &) = delete;

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] const CSTNode &Root() const;
    [[nodiscard]] CSTNode &Root();
};

class CSTParseException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

CST ParseTokensToCST(const compiler::lr1::LR1ParseTable &table,
                     const std::vector<GenericToken> &tokens);
std::string CSTToGraphvizDot(const CST &cst,
                             std::string_view graph_name = "cst");
const compiler::lr1::FlattenedProduction *
TryGetCSTReductionProduction(const compiler::lr1::LR1ParseTable &table,
                             const CSTNode &node);
const compiler::lr1::FlattenedProduction &
GetCSTReductionProduction(const compiler::lr1::LR1ParseTable &table,
                          const CSTNode &node);
bool CSTNodeMatchesProduction(const compiler::lr1::LR1ParseTable &table,
                              const CSTNode &node, std::string_view lhs,
                              std::initializer_list<std::string_view> rhs);

} // namespace compiler::parsergen1
