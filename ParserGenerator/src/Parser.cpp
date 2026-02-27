#include "ParserGenerator.h"

#include "Common/Graphviz.h"
#include "Common/Identifier.h"
#include "Stage2SpecLexer.h"
#include "Stage2SpecParser.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace compiler::parsergen {

namespace {

using SpecLexer = compiler::parsergen::detail::Stage2SpecLexer;
using SpecToken = compiler::parsergen::detail::Token;
using SpecTokenKind = compiler::parsergen::detail::Stage2SpecTokenKind;
using SpecParser =
    generated::ParserGeneratorStage2Spec::ParserGeneratorStage2SpecParser;

const char *TerminalNameForToken(SpecTokenKind kind) {
    switch (kind) {
    case SpecTokenKind::KW_GRAMMAR:
        return "KW_GRAMMAR";
    case SpecTokenKind::KW_START:
        return "KW_START";
    case SpecTokenKind::KW_TOKEN:
        return "KW_TOKEN";
    case SpecTokenKind::KW_RULE:
        return "KW_RULE";
    case SpecTokenKind::KW_AST:
        return "KW_AST";
    case SpecTokenKind::KW_ASTBASE:
        return "KW_ASTBASE";
    case SpecTokenKind::KW_LEXEME:
        return "KW_LEXEME";
    case SpecTokenKind::KW_VIRTUAL:
        return "KW_VIRTUAL";
    case SpecTokenKind::KW_OVERRIDE:
        return "KW_OVERRIDE";
    case SpecTokenKind::KW_CPP:
        return "KW_CPP";
    case SpecTokenKind::IDENT:
        return "IDENT";
    case SpecTokenKind::INT:
        return "INT";
    case SpecTokenKind::ARROW:
        return "ARROW";
    case SpecTokenKind::FATARROW:
        return "FATARROW";
    case SpecTokenKind::PIPE:
        return "PIPE";
    case SpecTokenKind::SEMI:
        return "SEMI";
    case SpecTokenKind::LPAREN:
        return "LPAREN";
    case SpecTokenKind::RPAREN:
        return "RPAREN";
    case SpecTokenKind::LBRACE:
        return "LBRACE";
    case SpecTokenKind::RBRACE:
        return "RBRACE";
    case SpecTokenKind::LBRACKET:
        return "LBRACKET";
    case SpecTokenKind::RBRACKET:
        return "RBRACKET";
    case SpecTokenKind::COMMA:
        return "COMMA";
    case SpecTokenKind::COLON:
        return "COLON";
    case SpecTokenKind::DOLLAR:
        return "DOLLAR";
    case SpecTokenKind::DOT:
        return "DOT";
    case SpecTokenKind::EQUAL:
        return "EQUAL";
    case SpecTokenKind::CODE:
        return "CODE";
    case SpecTokenKind::EndOfFile:
        return "$";
    }
    return "<invalid>";
}

std::vector<GenericToken>
ToGenericTokens(const std::vector<SpecToken> &tokens) {
    std::vector<GenericToken> out;
    out.reserve(tokens.size());
    for (const SpecToken &token : tokens) {
        out.push_back(GenericToken{
            .kind = TerminalNameForToken(token.kind),
            .lexeme = std::string(token.lexeme),
            .line = token.line,
            .column = token.column,
        });
    }
    return out;
}

bool MatchesReduction(const LR1ParseTable &parse_table, const CSTNode &node,
                      std::string_view lhs,
                      std::initializer_list<std::string_view> rhs) {
    return compiler::parsergen::CSTNodeMatchesProduction(parse_table, node, lhs,
                                                         rhs);
}

std::string ExpectTerminalLexeme(const CSTNode &node) {
    if (!node.IsTerminal()) {
        throw BuildException("expected terminal CST node, got '" +
                             std::string(node.Symbol()) + "'");
    }
    return std::string(node.Lexeme());
}

std::size_t ParseOneBasedIndex(const CSTNode &int_token_node) {
    const std::string text = ExpectTerminalLexeme(int_token_node);
    char *end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || value == 0ULL) {
        throw BuildException("invalid 1-based index literal: " + text);
    }
    if (value > static_cast<unsigned long long>(
                    std::numeric_limits<std::size_t>::max())) {
        throw BuildException("index literal out of range: " + text);
    }
    return static_cast<std::size_t>(value);
}

ASTNodeTypeDecl::FieldDecl ParseFieldSpec(const CSTNode &node,
                                          const LR1ParseTable &parse_table) {
    if (node.Symbol() != "FieldSpec") {
        throw BuildException("expected FieldSpec node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "FieldSpec", {"IDENT"})) {
        const std::string name = ExpectTerminalLexeme(node.Child(0));
        return ASTNodeTypeDecl::FieldDecl{.name = name, .type_name = {}};
    }
    auto parse_type_spec =
        [&](const CSTNode &type_node) -> ASTNodeTypeDecl::FieldDecl {
        if (type_node.Symbol() != "TypeSpec") {
            throw BuildException("expected TypeSpec node, got '" +
                                 std::string(type_node.Symbol()) + "'");
        }
        if (MatchesReduction(parse_table, type_node, "TypeSpec", {"IDENT"})) {
            return ASTNodeTypeDecl::FieldDecl{
                .name = ExpectTerminalLexeme(node.Child(0)),
                .type_name = ExpectTerminalLexeme(type_node.Child(0)),
                .is_list = false,
            };
        }
        if (MatchesReduction(parse_table, type_node, "TypeSpec",
                             {"IDENT", "LBRACKET", "RBRACKET"})) {
            return ASTNodeTypeDecl::FieldDecl{
                .name = ExpectTerminalLexeme(node.Child(0)),
                .type_name = ExpectTerminalLexeme(type_node.Child(0)),
                .is_list = true,
            };
        }
        throw BuildException("unsupported TypeSpec CST shape");
    };

    if (MatchesReduction(parse_table, node, "FieldSpec",
                         {"IDENT", "COLON", "TypeSpec"})) {
        return parse_type_spec(node.Child(2));
    }
    if (MatchesReduction(parse_table, node, "FieldSpec",
                         {"IDENT", "COLON", "IDENT"})) {
        return ASTNodeTypeDecl::FieldDecl{
            .name = ExpectTerminalLexeme(node.Child(0)),
            .type_name = ExpectTerminalLexeme(node.Child(2)),
            .is_list = false,
        };
    }

    throw BuildException("unsupported FieldSpec CST shape");
}

std::vector<ASTNodeTypeDecl::FieldDecl>
ParseFieldSpecList(const CSTNode &node, const LR1ParseTable &parse_table) {
    if (node.Symbol() != "FieldSpecList") {
        throw BuildException("expected FieldSpecList node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "FieldSpecList", {"FieldSpec"})) {
        return {ParseFieldSpec(node.Child(0), parse_table)};
    }
    if (MatchesReduction(parse_table, node, "FieldSpecList",
                         {"FieldSpecList", "COMMA", "FieldSpec"})) {
        std::vector<ASTNodeTypeDecl::FieldDecl> out =
            ParseFieldSpecList(node.Child(0), parse_table);
        out.push_back(ParseFieldSpec(node.Child(2), parse_table));
        return out;
    }

    throw BuildException("unsupported FieldSpecList CST shape");
}

ASTNodeTypeDecl::VirtualMethodDecl
ParseVirtualMethodDecl(const CSTNode &node, const LR1ParseTable &parse_table) {
    if (node.Symbol() != "VirtualMethodDecl") {
        throw BuildException("expected VirtualMethodDecl node, got '" +
                             std::string(node.Symbol()) + "'");
    }
    if (!MatchesReduction(parse_table, node, "VirtualMethodDecl",
                          {"KW_VIRTUAL", "IDENT", "LPAREN", "RPAREN", "ARROW",
                           "IDENT", "SEMI"})) {
        throw BuildException("unsupported VirtualMethodDecl CST shape");
    }

    return ASTNodeTypeDecl::VirtualMethodDecl{
        .name = ExpectTerminalLexeme(node.Child(1)),
        .return_type_name = ExpectTerminalLexeme(node.Child(5)),
    };
}

void AppendVirtualMethodDeclList(
    const CSTNode &node, const LR1ParseTable &parse_table,
    std::vector<ASTNodeTypeDecl::VirtualMethodDecl> &out) {
    if (node.Symbol() != "VirtualMethodDeclList") {
        throw BuildException("expected VirtualMethodDeclList node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "VirtualMethodDeclList",
                         {"VirtualMethodDecl"})) {
        out.push_back(ParseVirtualMethodDecl(node.Child(0), parse_table));
        return;
    }
    if (MatchesReduction(parse_table, node, "VirtualMethodDeclList",
                         {"VirtualMethodDeclList", "VirtualMethodDecl"})) {
        AppendVirtualMethodDeclList(node.Child(0), parse_table, out);
        out.push_back(ParseVirtualMethodDecl(node.Child(1), parse_table));
        return;
    }

    throw BuildException("unsupported VirtualMethodDeclList CST shape");
}

std::string ParseCodeLiteral(const CSTNode &code_token_node) {
    const std::string lexeme = ExpectTerminalLexeme(code_token_node);
    if (lexeme.size() < 2 || lexeme.front() != '`' || lexeme.back() != '`') {
        throw BuildException("expected backtick-delimited CODE token");
    }
    return lexeme.substr(1, lexeme.size() - 2);
}

ASTNodeTypeDecl::VirtualMethodImplDecl
ParseMethodImplDecl(const CSTNode &node, const LR1ParseTable &parse_table) {
    if (node.Symbol() != "MethodImplDecl") {
        throw BuildException("expected MethodImplDecl node, got '" +
                             std::string(node.Symbol()) + "'");
    }
    if (!MatchesReduction(parse_table, node, "MethodImplDecl",
                          {"KW_OVERRIDE", "IDENT", "LPAREN", "RPAREN", "ARROW",
                           "IDENT", "EQUAL", "CODE", "SEMI"})) {
        throw BuildException("unsupported MethodImplDecl CST shape");
    }

    return ASTNodeTypeDecl::VirtualMethodImplDecl{
        .name = ExpectTerminalLexeme(node.Child(1)),
        .return_type_name = ExpectTerminalLexeme(node.Child(5)),
        .body_cpp = ParseCodeLiteral(node.Child(7)),
    };
}

void AppendMethodImplDeclList(
    const CSTNode &node, const LR1ParseTable &parse_table,
    std::vector<ASTNodeTypeDecl::VirtualMethodImplDecl> &out) {
    if (node.Symbol() != "MethodImplDeclList") {
        throw BuildException("expected MethodImplDeclList node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "MethodImplDeclList",
                         {"MethodImplDecl"})) {
        out.push_back(ParseMethodImplDecl(node.Child(0), parse_table));
        return;
    }
    if (MatchesReduction(parse_table, node, "MethodImplDeclList",
                         {"MethodImplDeclList", "MethodImplDecl"})) {
        AppendMethodImplDeclList(node.Child(0), parse_table, out);
        out.push_back(ParseMethodImplDecl(node.Child(1), parse_table));
        return;
    }

    throw BuildException("unsupported MethodImplDeclList CST shape");
}

void FinalizeAstDeclFields(
    ASTNodeTypeDecl &decl,
    std::vector<ASTNodeTypeDecl::FieldDecl> field_decls) {
    decl.field_decls = std::move(field_decls);
    decl.fields.clear();
    decl.fields.reserve(decl.field_decls.size());
    for (const ASTNodeTypeDecl::FieldDecl &field : decl.field_decls) {
        decl.fields.push_back(field.name);
    }
}

std::vector<std::string> ParseSymbolList(const CSTNode &node,
                                         const LR1ParseTable &parse_table) {
    if (node.Symbol() != "SymbolList") {
        throw BuildException("expected SymbolList node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "SymbolList", {"IDENT"})) {
        return {ExpectTerminalLexeme(node.Child(0))};
    }
    if (MatchesReduction(parse_table, node, "SymbolList",
                         {"SymbolList", "IDENT"})) {
        std::vector<std::string> out =
            ParseSymbolList(node.Child(0), parse_table);
        out.push_back(ExpectTerminalLexeme(node.Child(1)));
        return out;
    }

    throw BuildException("unsupported SymbolList CST shape");
}

ActionArg ParseActionArg(const CSTNode &node,
                         const LR1ParseTable &parse_table) {
    if (node.Symbol() != "ActionArg") {
        throw BuildException("expected ActionArg node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "ActionArg", {"DOLLAR", "INT"})) {
        return ActionArg{.kind = ActionArgKind::ChildAST,
                         .rhs_index = ParseOneBasedIndex(node.Child(1))};
    }
    if (MatchesReduction(parse_table, node, "ActionArg",
                         {"DOLLAR", "INT", "DOT", "KW_LEXEME"})) {
        return ActionArg{.kind = ActionArgKind::ChildLexeme,
                         .rhs_index = ParseOneBasedIndex(node.Child(1))};
    }

    throw BuildException("unsupported ActionArg CST shape");
}

std::vector<ActionArg> ParseActionArgList(const CSTNode &node,
                                          const LR1ParseTable &parse_table) {
    if (node.Symbol() != "ActionArgList") {
        throw BuildException("expected ActionArgList node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "ActionArgList", {"ActionArg"})) {
        return {ParseActionArg(node.Child(0), parse_table)};
    }
    if (MatchesReduction(parse_table, node, "ActionArgList",
                         {"ActionArgList", "COMMA", "ActionArg"})) {
        std::vector<ActionArg> out =
            ParseActionArgList(node.Child(0), parse_table);
        out.push_back(ParseActionArg(node.Child(2), parse_table));
        return out;
    }

    throw BuildException("unsupported ActionArgList CST shape");
}

RuleAction ParseActionExpr(const CSTNode &node,
                           const LR1ParseTable &parse_table) {
    if (node.Symbol() != "ActionExpr") {
        throw BuildException("expected ActionExpr node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    std::string callee;
    std::vector<ActionArg> args;

    if (MatchesReduction(parse_table, node, "ActionExpr", {"KW_CPP", "CODE"})) {
        return RuleAction{
            .kind = RuleActionKind::InlineCpp,
            .target_node_name = {},
            .pass_rhs_index = 0,
            .args = {},
            .cpp_code = ParseCodeLiteral(node.Child(1)),
        };
    }
    if (MatchesReduction(parse_table, node, "ActionExpr",
                         {"IDENT", "LPAREN", "RPAREN"})) {
        callee = ExpectTerminalLexeme(node.Child(0));
    } else if (MatchesReduction(
                   parse_table, node, "ActionExpr",
                   {"IDENT", "LPAREN", "ActionArgList", "RPAREN"})) {
        callee = ExpectTerminalLexeme(node.Child(0));
        args = ParseActionArgList(node.Child(2), parse_table);
    } else {
        throw BuildException("unsupported ActionExpr CST shape");
    }

    if (callee == "Pass") {
        if (args.size() != 1 || args[0].kind != ActionArgKind::ChildAST) {
            throw BuildException("Pass(...) requires exactly one child AST "
                                 "argument like Pass($1)");
        }
        return RuleAction{
            .kind = RuleActionKind::Pass,
            .target_node_name = {},
            .pass_rhs_index = args[0].rhs_index,
            .args = {},
        };
    }

    return RuleAction{
        .kind = RuleActionKind::Construct,
        .target_node_name = std::move(callee),
        .pass_rhs_index = 0,
        .args = std::move(args),
        .cpp_code = {},
    };
}

RuleAlternative ParseRuleAlt(const CSTNode &node,
                             const LR1ParseTable &parse_table) {
    if (MatchesReduction(parse_table, node, "RuleAlt", {"SymbolList"})) {
        return RuleAlternative{
            .symbols = ParseSymbolList(node.Child(0), parse_table),
            .action = RuleAction{},
        };
    }
    if (MatchesReduction(parse_table, node, "RuleAlt",
                         {"SymbolList", "FATARROW", "ActionExpr"})) {
        return RuleAlternative{
            .symbols = ParseSymbolList(node.Child(0), parse_table),
            .action = ParseActionExpr(node.Child(2), parse_table),
        };
    }

    throw BuildException("unsupported RuleAlt CST shape");
}

void AppendRuleAltList(const CSTNode &node, const LR1ParseTable &parse_table,
                       std::vector<RuleAlternative> &out) {
    if (node.Symbol() != "RuleAltList") {
        throw BuildException("expected RuleAltList node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "RuleAltList", {"RuleAlt"})) {
        out.push_back(ParseRuleAlt(node.Child(0), parse_table));
        return;
    }
    if (MatchesReduction(parse_table, node, "RuleAltList",
                         {"RuleAltList", "PIPE", "RuleAlt"})) {
        AppendRuleAltList(node.Child(0), parse_table, out);
        out.push_back(ParseRuleAlt(node.Child(2), parse_table));
        return;
    }

    throw BuildException("unsupported RuleAltList CST shape");
}

void ParseDecl(const CSTNode &node, const LR1ParseTable &parse_table,
               Stage2SpecAST &spec);

void AppendDeclList(const CSTNode &node, const LR1ParseTable &parse_table,
                    Stage2SpecAST &spec) {
    if (node.Symbol() != "DeclList") {
        throw BuildException("expected DeclList node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "DeclList", {"Decl"})) {
        ParseDecl(node.Child(0), parse_table, spec);
        return;
    }
    if (MatchesReduction(parse_table, node, "DeclList", {"DeclList", "Decl"})) {
        AppendDeclList(node.Child(0), parse_table, spec);
        ParseDecl(node.Child(1), parse_table, spec);
        return;
    }

    throw BuildException("unsupported DeclList CST shape");
}

void ParseDecl(const CSTNode &node, const LR1ParseTable &parse_table,
               Stage2SpecAST &spec) {
    if (node.Symbol() != "Decl") {
        throw BuildException("expected Decl node, got '" +
                             std::string(node.Symbol()) + "'");
    }

    if (MatchesReduction(parse_table, node, "Decl", {"GrammarDecl"})) {
        const CSTNode &g = node.Child(0);
        if (!spec.grammar_name.empty()) {
            throw BuildException("duplicate grammar declaration");
        }
        spec.grammar_name = ExpectTerminalLexeme(g.Child(1));
        return;
    }
    if (MatchesReduction(parse_table, node, "Decl", {"StartDecl"})) {
        const CSTNode &s = node.Child(0);
        if (!spec.start_symbol.empty()) {
            throw BuildException("duplicate start declaration");
        }
        spec.start_symbol = ExpectTerminalLexeme(s.Child(1));
        return;
    }
    if (MatchesReduction(parse_table, node, "Decl", {"TokenDecl"})) {
        const CSTNode &t = node.Child(0);
        spec.terminals.push_back(ExpectTerminalLexeme(t.Child(1)));
        return;
    }
    if (MatchesReduction(parse_table, node, "Decl", {"AstBaseDecl"})) {
        const CSTNode &a = node.Child(0);
        ASTNodeTypeDecl decl;
        decl.is_abstract = true;
        decl.name = ExpectTerminalLexeme(a.Child(1));
        decl.base_type_name.clear();

        if (MatchesReduction(parse_table, a, "AstBaseDecl",
                             {"KW_ASTBASE", "IDENT", "SEMI"})) {
            // no base, no methods
        } else if (MatchesReduction(
                       parse_table, a, "AstBaseDecl",
                       {"KW_ASTBASE", "IDENT", "COLON", "IDENT", "SEMI"})) {
            decl.base_type_name = ExpectTerminalLexeme(a.Child(3));
        } else if (MatchesReduction(parse_table, a, "AstBaseDecl",
                                    {"KW_ASTBASE", "IDENT", "LBRACE",
                                     "VirtualMethodDeclList", "RBRACE",
                                     "SEMI"})) {
            AppendVirtualMethodDeclList(a.Child(3), parse_table,
                                        decl.virtual_methods);
        } else if (MatchesReduction(parse_table, a, "AstBaseDecl",
                                    {"KW_ASTBASE", "IDENT", "COLON", "IDENT",
                                     "LBRACE", "VirtualMethodDeclList",
                                     "RBRACE", "SEMI"})) {
            decl.base_type_name = ExpectTerminalLexeme(a.Child(3));
            AppendVirtualMethodDeclList(a.Child(5), parse_table,
                                        decl.virtual_methods);
        } else {
            throw BuildException("unsupported AstBaseDecl CST shape");
        }

        FinalizeAstDeclFields(decl, {});
        spec.ast_node_types.push_back(std::move(decl));
        return;
    }
    if (MatchesReduction(parse_table, node, "Decl", {"AstDecl"})) {
        const CSTNode &a = node.Child(0);
        ASTNodeTypeDecl decl;
        decl.is_abstract = false;
        decl.name = ExpectTerminalLexeme(a.Child(1));
        decl.base_type_name.clear();

        if (MatchesReduction(parse_table, a, "AstDecl",
                             {"KW_AST", "IDENT", "LPAREN", "RPAREN", "SEMI"})) {
            FinalizeAstDeclFields(decl, {});
        } else if (MatchesReduction(parse_table, a, "AstDecl",
                                    {"KW_AST", "IDENT", "COLON", "IDENT",
                                     "LPAREN", "RPAREN", "SEMI"})) {
            decl.base_type_name = ExpectTerminalLexeme(a.Child(3));
            FinalizeAstDeclFields(decl, {});
        } else if (MatchesReduction(parse_table, a, "AstDecl",
                                    {"KW_AST", "IDENT", "LPAREN",
                                     "FieldSpecList", "RPAREN", "SEMI"})) {
            FinalizeAstDeclFields(decl,
                                  ParseFieldSpecList(a.Child(3), parse_table));
        } else if (MatchesReduction(parse_table, a, "AstDecl",
                                    {"KW_AST", "IDENT", "COLON", "IDENT",
                                     "LPAREN", "FieldSpecList", "RPAREN",
                                     "SEMI"})) {
            decl.base_type_name = ExpectTerminalLexeme(a.Child(3));
            FinalizeAstDeclFields(decl,
                                  ParseFieldSpecList(a.Child(5), parse_table));
        } else if (MatchesReduction(parse_table, a, "AstDecl",
                                    {"KW_AST", "IDENT", "LPAREN", "RPAREN",
                                     "LBRACE", "MethodImplDeclList", "RBRACE",
                                     "SEMI"})) {
            FinalizeAstDeclFields(decl, {});
            AppendMethodImplDeclList(a.Child(5), parse_table,
                                     decl.virtual_method_impls);
        } else if (MatchesReduction(parse_table, a, "AstDecl",
                                    {"KW_AST", "IDENT", "COLON", "IDENT",
                                     "LPAREN", "RPAREN", "LBRACE",
                                     "MethodImplDeclList", "RBRACE", "SEMI"})) {
            decl.base_type_name = ExpectTerminalLexeme(a.Child(3));
            FinalizeAstDeclFields(decl, {});
            AppendMethodImplDeclList(a.Child(7), parse_table,
                                     decl.virtual_method_impls);
        } else if (MatchesReduction(parse_table, a, "AstDecl",
                                    {"KW_AST", "IDENT", "LPAREN",
                                     "FieldSpecList", "RPAREN", "LBRACE",
                                     "MethodImplDeclList", "RBRACE", "SEMI"})) {
            FinalizeAstDeclFields(decl,
                                  ParseFieldSpecList(a.Child(3), parse_table));
            AppendMethodImplDeclList(a.Child(6), parse_table,
                                     decl.virtual_method_impls);
        } else if (MatchesReduction(parse_table, a, "AstDecl",
                                    {"KW_AST", "IDENT", "COLON", "IDENT",
                                     "LPAREN", "FieldSpecList", "RPAREN",
                                     "LBRACE", "MethodImplDeclList", "RBRACE",
                                     "SEMI"})) {
            decl.base_type_name = ExpectTerminalLexeme(a.Child(3));
            FinalizeAstDeclFields(decl,
                                  ParseFieldSpecList(a.Child(5), parse_table));
            AppendMethodImplDeclList(a.Child(8), parse_table,
                                     decl.virtual_method_impls);
        } else {
            throw BuildException("unsupported AstDecl CST shape");
        }
        spec.ast_node_types.push_back(std::move(decl));
        return;
    }
    if (MatchesReduction(parse_table, node, "Decl", {"RuleDecl"})) {
        const CSTNode &r = node.Child(0);
        RuleDefinition rule;
        rule.lhs = ExpectTerminalLexeme(r.Child(1));
        AppendRuleAltList(r.Child(3), parse_table, rule.alternatives);
        spec.rules.push_back(std::move(rule));
        return;
    }

    throw BuildException("unsupported Decl CST shape");
}

std::string ActionArgToString(const ActionArg &arg) {
    std::ostringstream oss;
    oss << "$" << arg.rhs_index;
    if (arg.kind == ActionArgKind::ChildLexeme) {
        oss << ".lexeme";
    }
    return oss.str();
}

std::string RuleActionToString(const RuleAction &action) {
    std::ostringstream oss;
    if (action.kind == RuleActionKind::None) {
        return {};
    }
    if (action.kind == RuleActionKind::Pass) {
        oss << "Pass($" << action.pass_rhs_index << ")";
        return oss.str();
    }
    if (action.kind == RuleActionKind::InlineCpp) {
        std::string preview = action.cpp_code;
        constexpr std::size_t kMaxPreview = 48;
        if (preview.size() > kMaxPreview) {
            preview.resize(kMaxPreview);
            preview += "...";
        }
        oss << "cpp {" << preview << "}";
        return oss.str();
    }

    oss << action.target_node_name << "(";
    for (std::size_t i = 0; i < action.args.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << ActionArgToString(action.args[i]);
    }
    oss << ")";
    return oss.str();
}

struct ValidationContext {
    std::unordered_set<std::string> terminal_set;
    std::unordered_set<std::string> nonterminal_set;
    std::unordered_map<std::string, const ASTNodeTypeDecl *> ast_types_by_name;
};

bool IsBuiltinAstFieldType(std::string_view type_name) {
    return type_name == "string" || type_name == "text";
}

ValidationContext BuildValidationContext(const Stage2SpecAST &spec) {
    ValidationContext ctx;
    for (const std::string &terminal : spec.terminals) {
        if (!ctx.terminal_set.insert(terminal).second) {
            throw BuildException("duplicate token declaration: " + terminal);
        }
    }
    for (const RuleDefinition &rule : spec.rules) {
        if (!ctx.nonterminal_set.insert(rule.lhs).second) {
            throw BuildException(
                "duplicate rule declaration for nonterminal: " + rule.lhs);
        }
    }
    for (const ASTNodeTypeDecl &node : spec.ast_node_types) {
        if (ctx.ast_types_by_name.find(node.name) !=
            ctx.ast_types_by_name.end()) {
            throw BuildException("duplicate AST node type declaration: " +
                                 node.name);
        }
        ctx.ast_types_by_name[node.name] = &node;

        std::unordered_set<std::string> field_names;
        for (const ASTNodeTypeDecl::FieldDecl &field : node.field_decls) {
            if (!field_names.insert(field.name).second) {
                throw BuildException("duplicate field '" + field.name +
                                     "' in AST node type '" + node.name + "'");
            }
        }
        std::unordered_set<std::string> method_names;
        for (const ASTNodeTypeDecl::VirtualMethodDecl &method :
             node.virtual_methods) {
            if (!method_names.insert(method.name).second) {
                throw BuildException("duplicate virtual method '" +
                                     method.name + "' in AST type '" +
                                     node.name + "'");
            }
        }
        std::unordered_set<std::string> method_impl_names;
        for (const ASTNodeTypeDecl::VirtualMethodImplDecl &impl :
             node.virtual_method_impls) {
            if (!method_impl_names.insert(impl.name).second) {
                throw BuildException(
                    "duplicate virtual method implementation '" + impl.name +
                    "' in AST type '" + node.name + "'");
            }
        }
        if (node.is_abstract && !node.field_decls.empty()) {
            throw BuildException(
                "astbase '" + node.name +
                "' cannot declare fields; use ast for concrete node types");
        }
    }

    for (const std::string &terminal : ctx.terminal_set) {
        if (ctx.nonterminal_set.find(terminal) != ctx.nonterminal_set.end()) {
            throw BuildException("symbol declared as both token and rule: " +
                                 terminal);
        }
    }

    for (const ASTNodeTypeDecl &node : spec.ast_node_types) {
        if (!node.base_type_name.empty()) {
            const auto base_it =
                ctx.ast_types_by_name.find(node.base_type_name);
            if (base_it == ctx.ast_types_by_name.end()) {
                throw BuildException("unknown AST base type '" +
                                     node.base_type_name + "' for '" +
                                     node.name + "'");
            }
            if (!base_it->second->is_abstract) {
                throw BuildException("AST type '" + node.name +
                                     "' must inherit from an astbase, but '" +
                                     node.base_type_name +
                                     "' is not declared with astbase");
            }
        }

        for (const ASTNodeTypeDecl::FieldDecl &field : node.field_decls) {
            if (field.type_name.empty()) {
                continue; // legacy/untyped field syntax remains supported
            }
            if (IsBuiltinAstFieldType(field.type_name)) {
                continue;
            }
            if (ctx.ast_types_by_name.find(field.type_name) ==
                ctx.ast_types_by_name.end()) {
                throw BuildException("unknown AST field type '" +
                                     field.type_name + "' for field '" +
                                     field.name + "' in AST type '" +
                                     node.name + "'");
            }
        }
    }

    return ctx;
}

void CollectInheritedVirtualMethods(
    const ASTNodeTypeDecl &type_decl, const ValidationContext &ctx,
    std::unordered_map<std::string, std::string> &methods_out,
    std::unordered_set<std::string> &visiting) {
    if (!visiting.insert(type_decl.name).second) {
        throw BuildException("cyclic AST base inheritance involving '" +
                             type_decl.name + "'");
    }

    if (!type_decl.base_type_name.empty()) {
        const auto base_it =
            ctx.ast_types_by_name.find(type_decl.base_type_name);
        if (base_it == ctx.ast_types_by_name.end()) {
            throw BuildException("unknown AST base type '" +
                                 type_decl.base_type_name + "' for '" +
                                 type_decl.name + "'");
        }
        CollectInheritedVirtualMethods(*base_it->second, ctx, methods_out,
                                       visiting);
    }

    for (const ASTNodeTypeDecl::VirtualMethodDecl &method :
         type_decl.virtual_methods) {
        const auto found = methods_out.find(method.name);
        if (found != methods_out.end() &&
            found->second != method.return_type_name) {
            throw BuildException(
                "virtual method '" + method.name + "' in AST type '" +
                type_decl.name + "' changes return type from '" +
                found->second + "' to '" + method.return_type_name + "'");
        }
        methods_out[method.name] = method.return_type_name;
    }

    visiting.erase(type_decl.name);
}

void ValidateMethodImplementations(const ASTNodeTypeDecl &node,
                                   const ValidationContext &ctx) {
    if (node.virtual_method_impls.empty()) {
        return;
    }
    if (node.is_abstract) {
        throw BuildException(
            "astbase '" + node.name +
            "' cannot contain override method implementations");
    }
    if (node.base_type_name.empty()) {
        throw BuildException("AST node type '" + node.name +
                             "' declares override methods but has no astbase "
                             "parent to override");
    }

    std::unordered_map<std::string, std::string> inherited_virtual_methods;
    std::unordered_set<std::string> visiting;
    const auto base_it = ctx.ast_types_by_name.find(node.base_type_name);
    if (base_it == ctx.ast_types_by_name.end()) {
        throw BuildException("unknown AST base type '" + node.base_type_name +
                             "' for '" + node.name + "'");
    }
    CollectInheritedVirtualMethods(*base_it->second, ctx,
                                   inherited_virtual_methods, visiting);

    std::unordered_set<std::string> seen_impl_names;
    for (const ASTNodeTypeDecl::VirtualMethodImplDecl &impl :
         node.virtual_method_impls) {
        if (!seen_impl_names.insert(impl.name).second) {
            throw BuildException("duplicate override method '" + impl.name +
                                 "' in AST node type '" + node.name + "'");
        }
        const auto method_it = inherited_virtual_methods.find(impl.name);
        if (method_it == inherited_virtual_methods.end()) {
            throw BuildException("override method '" + impl.name +
                                 "' in AST node type '" + node.name +
                                 "' does not exist in its astbase hierarchy");
        }
        if (method_it->second != impl.return_type_name) {
            throw BuildException(
                "override method '" + impl.name + "' in AST node type '" +
                node.name + "' uses return type '" + impl.return_type_name +
                "' but base declares '" + method_it->second + "'");
        }
        if (impl.body_cpp.empty()) {
            throw BuildException("override method '" + impl.name +
                                 "' in AST node type '" + node.name +
                                 "' has an empty body");
        }
    }
}

void ValidateRuleAlternative(const RuleAlternative &alt,
                             const ValidationContext &ctx,
                             const std::string &lhs) {
    if (alt.symbols.empty()) {
        throw BuildException("empty RHS is not currently supported (rule '" +
                             lhs + "')");
    }

    for (const std::string &symbol : alt.symbols) {
        if (ctx.terminal_set.find(symbol) == ctx.terminal_set.end() &&
            ctx.nonterminal_set.find(symbol) == ctx.nonterminal_set.end()) {
            throw BuildException("unknown symbol '" + symbol + "' in rule '" +
                                 lhs + "'");
        }
    }

    const auto check_rhs_ref = [&](std::size_t rhs_index) {
        if (rhs_index == 0 || rhs_index > alt.symbols.size()) {
            throw BuildException("action references out-of-range RHS index $" +
                                 std::to_string(rhs_index) + " in rule '" +
                                 lhs + "'");
        }
    };

    if (alt.action.kind == RuleActionKind::None) {
        return;
    }

    if (alt.action.kind == RuleActionKind::InlineCpp) {
        if (alt.action.cpp_code.empty()) {
            throw BuildException("inline cpp action is empty in rule '" + lhs +
                                 "'");
        }
        return;
    }

    if (alt.action.kind == RuleActionKind::Pass) {
        check_rhs_ref(alt.action.pass_rhs_index);
        const std::string &symbol = alt.symbols[alt.action.pass_rhs_index - 1];
        if (ctx.terminal_set.find(symbol) != ctx.terminal_set.end()) {
            throw BuildException("Pass($" +
                                 std::to_string(alt.action.pass_rhs_index) +
                                 ") cannot forward terminal symbol '" + symbol +
                                 "'; use .lexeme in a constructor action");
        }
        return;
    }

    const auto it = ctx.ast_types_by_name.find(alt.action.target_node_name);
    if (it == ctx.ast_types_by_name.end()) {
        throw BuildException("unknown AST node type in action: " +
                             alt.action.target_node_name);
    }

    const ASTNodeTypeDecl &ast_decl = *it->second;
    if (ast_decl.is_abstract) {
        throw BuildException("action for rule '" + lhs +
                             "' cannot construct astbase '" + ast_decl.name +
                             "'");
    }
    if (ast_decl.fields.size() != alt.action.args.size()) {
        throw BuildException(
            "action for rule '" + lhs + "' constructs '" + ast_decl.name +
            "' with " + std::to_string(alt.action.args.size()) +
            " arg(s), but AST node type declares " +
            std::to_string(ast_decl.fields.size()) + " field(s)");
    }

    for (const ActionArg &arg : alt.action.args) {
        check_rhs_ref(arg.rhs_index);
        const std::string &symbol = alt.symbols[arg.rhs_index - 1];
        const bool is_terminal =
            (ctx.terminal_set.find(symbol) != ctx.terminal_set.end());
        if (arg.kind == ActionArgKind::ChildAST && is_terminal) {
            throw BuildException("action argument $" +
                                 std::to_string(arg.rhs_index) +
                                 " refers to terminal symbol '" + symbol +
                                 "'; use .lexeme instead");
        }
        if (arg.kind == ActionArgKind::ChildLexeme && !is_terminal) {
            throw BuildException(
                "action argument $" + std::to_string(arg.rhs_index) +
                ".lexeme refers to nonterminal symbol '" + symbol + "'");
        }
    }

    if (ast_decl.field_decls.size() == alt.action.args.size()) {
        for (std::size_t i = 0; i < ast_decl.field_decls.size(); ++i) {
            const ASTNodeTypeDecl::FieldDecl &field = ast_decl.field_decls[i];
            if (field.type_name.empty()) {
                continue;
            }
            const ActionArg &arg = alt.action.args[i];
            if (IsBuiltinAstFieldType(field.type_name)) {
                if (arg.kind != ActionArgKind::ChildLexeme) {
                    throw BuildException(
                        "field '" + field.name + "' in AST type '" +
                        ast_decl.name + "' has builtin text type '" +
                        field.type_name +
                        "' and must be populated with $N.lexeme");
                }
            } else {
                if (arg.kind != ActionArgKind::ChildAST) {
                    throw BuildException(
                        "field '" + field.name + "' in AST type '" +
                        ast_decl.name + "' has AST type '" + field.type_name +
                        "' and must be populated with an AST child ($N)");
                }
            }
            if (field.is_list && arg.kind == ActionArgKind::ChildLexeme &&
                !IsBuiltinAstFieldType(field.type_name)) {
                throw BuildException(
                    "list field '" + field.name + "' in AST type '" +
                    ast_decl.name + "' has AST element type '" +
                    field.type_name + "' and cannot be populated from .lexeme");
            }
            if (field.is_list && arg.kind == ActionArgKind::ChildAST &&
                IsBuiltinAstFieldType(field.type_name)) {
                throw BuildException(
                    "list field '" + field.name + "' in AST type '" +
                    ast_decl.name + "' has text element type '" +
                    field.type_name + "' and must be populated from .lexeme");
            }
        }
    }
}

const RuleAlternative &RuleAlternativeForNode(const Stage2SpecAST &spec,
                                              const LR1ParseTable &parse_table,
                                              const CSTNode &node) {
    const FlattenedProduction &production =
        compiler::parsergen::GetCSTReductionProduction(parse_table, node);
    if (production.source_rule_index >= spec.rules.size()) {
        throw BuildException("CST reduction has invalid source rule index");
    }
    const RuleDefinition &rule = spec.rules[production.source_rule_index];
    if (production.source_alternative_index >= rule.alternatives.size()) {
        throw BuildException(
            "CST reduction has invalid source alternative index");
    }
    return rule.alternatives[production.source_alternative_index];
}

const ASTNodeTypeDecl &FindASTNodeType(const Stage2SpecAST &spec,
                                       std::string_view name) {
    for (const ASTNodeTypeDecl &decl : spec.ast_node_types) {
        if (decl.name == name) {
            return decl;
        }
    }
    throw BuildException("unknown AST node type: " + std::string(name));
}

std::unique_ptr<GeneratedASTNode>
BuildGeneratedASTNodeRecursive(const CSTNode &node,
                               const LR1ParseTable &parse_table,
                               const Stage2SpecAST &spec) {
    if (node.IsTerminal()) {
        return nullptr;
    }

    const RuleAlternative &alt =
        RuleAlternativeForNode(spec, parse_table, node);
    const RuleAction &action = alt.action;

    if (action.kind == RuleActionKind::None) {
        throw BuildException("cannot build AST for reduction without an action "
                             "on rule alternative");
    }

    if (action.kind == RuleActionKind::InlineCpp) {
        throw BuildException("cannot build generic AST for inline cpp action; "
                             "use typed ParseToAST output");
    }

    if (action.kind == RuleActionKind::Pass) {
        return BuildGeneratedASTNodeRecursive(
            node.Child(action.pass_rhs_index - 1), parse_table, spec);
    }

    const ASTNodeTypeDecl &ast_decl =
        FindASTNodeType(spec, action.target_node_name);
    if (ast_decl.is_abstract) {
        throw BuildException("cannot construct abstract AST base '" +
                             ast_decl.name + "'");
    }
    std::vector<GeneratedASTNodeChildField> child_fields;
    std::vector<GeneratedASTNodeTextField> text_fields;
    child_fields.reserve(action.args.size());
    text_fields.reserve(action.args.size());

    for (std::size_t i = 0; i < action.args.size(); ++i) {
        const ActionArg &arg = action.args[i];
        const std::string &field_name = ast_decl.fields[i];
        const CSTNode &rhs_node = node.Child(arg.rhs_index - 1);
        if (arg.kind == ActionArgKind::ChildAST) {
            std::unique_ptr<GeneratedASTNode> child_ast =
                BuildGeneratedASTNodeRecursive(rhs_node, parse_table, spec);
            if (!child_ast) {
                throw BuildException("action attempted to use terminal RHS "
                                     "symbol as AST child: $" +
                                     std::to_string(arg.rhs_index));
            }
            child_fields.push_back(GeneratedASTNodeChildField{
                .name = field_name, .value = std::move(child_ast)});
            continue;
        }

        if (!rhs_node.IsTerminal()) {
            throw BuildException(
                ".lexeme action arg referenced nonterminal at runtime");
        }
        text_fields.push_back(GeneratedASTNodeTextField{
            .name = field_name, .value = std::string(rhs_node.Lexeme())});
    }

    return std::make_unique<GeneratedCustomASTNode>(action.target_node_name,
                                                    std::move(child_fields),
                                                    std::move(text_fields));
}

std::size_t EmitGeneratedASTGraphvizNode(const GeneratedASTNode &node,
                                         std::ostringstream &out,
                                         std::size_t &next_id) {
    const std::size_t id = next_id++;

    std::string label(node.KindName());
    for (const GeneratedASTNodeTextField &field : node.TextFields()) {
        label.push_back('\n');
        label += field.name;
        label.push_back('=');
        label += field.value;
    }
    out << "  n" << id << " [label=\""
        << compiler::common::EscapeGraphvizLabel(label) << "\"];\n";

    for (const GeneratedASTNodeChildField &child_field : node.ChildFields()) {
        const std::size_t child_id =
            EmitGeneratedASTGraphvizNode(*child_field.value, out, next_id);
        out << "  n" << id << " -> n" << child_id << " [label=\""
            << compiler::common::EscapeGraphvizLabel(child_field.name)
            << "\"];\n";
    }

    return id;
}

} // namespace

std::size_t GeneratedASTNode::ChildCount() const {
    return ChildFields().size();
}

const GeneratedASTNode &GeneratedASTNode::Child(std::size_t index) const {
    const auto &fields = ChildFields();
    if (index >= fields.size() || fields[index].value == nullptr) {
        throw std::out_of_range("GeneratedAST child index out of range");
    }
    return *fields[index].value;
}

GeneratedCustomASTNode::GeneratedCustomASTNode(
    std::string kind_name, std::vector<GeneratedASTNodeChildField> child_fields,
    std::vector<GeneratedASTNodeTextField> text_fields)
    : kind_name_(std::move(kind_name)), child_fields_(std::move(child_fields)),
      text_fields_(std::move(text_fields)) {}

std::string_view GeneratedCustomASTNode::KindName() const { return kind_name_; }

const std::vector<GeneratedASTNodeChildField> &
GeneratedCustomASTNode::ChildFields() const {
    return child_fields_;
}

const std::vector<GeneratedASTNodeTextField> &
GeneratedCustomASTNode::TextFields() const {
    return text_fields_;
}

bool GeneratedAST::Empty() const { return root == nullptr; }

const GeneratedASTNode &GeneratedAST::Root() const {
    if (root == nullptr) {
        throw BuildException("GeneratedAST root is empty");
    }
    return *root;
}

GeneratedASTNode &GeneratedAST::Root() {
    if (root == nullptr) {
        throw BuildException("GeneratedAST root is empty");
    }
    return *root;
}

Stage2SpecAST ParseStage2Spec(std::string_view source_text) {
    try {
        SpecLexer lexer(source_text);
        const std::vector<SpecToken> tokens = lexer.Tokenize();
        const std::vector<GenericToken> parser_tokens = ToGenericTokens(tokens);

        SpecParser parser;
        const CST cst = parser.Parse(parser_tokens);
        const LR1ParseTable &parse_table = SpecParser::ParseTable();

        Stage2SpecAST spec;
        const CSTNode &root = cst.Root();
        if (!MatchesReduction(parse_table, root, "File", {"DeclList"})) {
            throw BuildException(
                "unsupported File CST shape in stage2 spec parser");
        }
        AppendDeclList(root.Child(0), parse_table, spec);
        ValidateStage2Spec(spec);
        return spec;
    } catch (const compiler::parsergen::ParseException &) {
        throw;
    } catch (const compiler::parsergen::BuildException &) {
        throw;
    } catch (const compiler::parsergen::CSTParseException &ex) {
        throw BuildException(std::string("failed to parse stage2 spec CST: ") +
                             ex.what());
    }
}

void ValidateStage2Spec(const Stage2SpecAST &spec) {
    if (spec.grammar_name.empty()) {
        throw BuildException("missing grammar declaration");
    }
    if (spec.start_symbol.empty()) {
        throw BuildException("missing start declaration");
    }
    if (spec.rules.empty()) {
        throw BuildException("no rules declared");
    }

    const ValidationContext ctx = BuildValidationContext(spec);
    if (ctx.nonterminal_set.find(spec.start_symbol) ==
        ctx.nonterminal_set.end()) {
        throw BuildException("start symbol has no rule: " + spec.start_symbol);
    }

    for (const ASTNodeTypeDecl &node : spec.ast_node_types) {
        ValidateMethodImplementations(node, ctx);
    }

    for (const RuleDefinition &rule : spec.rules) {
        if (rule.alternatives.empty()) {
            throw BuildException("rule has no alternatives: " + rule.lhs);
        }
        for (const RuleAlternative &alt : rule.alternatives) {
            ValidateRuleAlternative(alt, ctx, rule.lhs);
        }
    }
}

GrammarSpecAST ToBaseGrammarSpec(const Stage2SpecAST &spec) {
    ValidateStage2Spec(spec);

    GrammarSpecAST out;
    out.grammar_name = spec.grammar_name;
    out.start_symbol = spec.start_symbol;
    out.terminals = spec.terminals;
    out.rules.reserve(spec.rules.size());

    for (const RuleDefinition &rule : spec.rules) {
        compiler::parsergen::ProductionRule base_rule;
        base_rule.lhs = rule.lhs;
        base_rule.alternatives.reserve(rule.alternatives.size());
        for (const RuleAlternative &alt : rule.alternatives) {
            compiler::parsergen::ProductionAlternative base_alt;
            base_alt.symbols = alt.symbols;
            base_rule.alternatives.push_back(std::move(base_alt));
        }
        out.rules.push_back(std::move(base_rule));
    }

    return out;
}

LR1CanonicalCollection BuildLR1CanonicalCollection(const Stage2SpecAST &spec) {
    return compiler::parsergen::BuildLR1CanonicalCollection(
        ToBaseGrammarSpec(spec));
}

LR1ParseTable BuildLR1ParseTable(const Stage2SpecAST &spec) {
    return compiler::parsergen::BuildLR1ParseTable(ToBaseGrammarSpec(spec));
}

LR1ParseTable BuildLR1ParseTableFromStage2Spec(std::string_view source_text) {
    const Stage2SpecAST spec = ParseStage2Spec(source_text);
    return BuildLR1ParseTable(spec);
}

std::string Stage2SpecASTToGraphvizDot(const Stage2SpecAST &spec,
                                       std::string_view graph_name) {
    std::ostringstream out;
    out << "digraph "
        << compiler::common::SanitizeIdentifier(
               graph_name, "parser_generator_stage2_spec_ast")
        << " {\n";
    out << "  rankdir=TB;\n";
    out << "  node [shape=box];\n";
    out << "  root [label=\"Spec\\ngrammar="
        << compiler::common::EscapeGraphvizLabel(spec.grammar_name)
        << "\\nstart="
        << compiler::common::EscapeGraphvizLabel(spec.start_symbol) << "\"];\n";

    std::size_t next_id = 0;

    out << "  tokens [label=\"Tokens\"];\n";
    out << "  root -> tokens;\n";
    for (const std::string &token : spec.terminals) {
        out << "  t" << next_id << " [label=\""
            << compiler::common::EscapeGraphvizLabel(token) << "\"];\n";
        out << "  tokens -> t" << next_id << ";\n";
        ++next_id;
    }

    out << "  astdecls [label=\"AST Node Types\"];\n";
    out << "  root -> astdecls;\n";
    for (const ASTNodeTypeDecl &decl : spec.ast_node_types) {
        std::ostringstream label;
        label << (decl.is_abstract ? "astbase " : "ast ") << decl.name;
        if (!decl.base_type_name.empty()) {
            label << " : " << decl.base_type_name;
        }
        if (!decl.fields.empty()) {
            label << "(";
            for (std::size_t i = 0; i < decl.fields.size(); ++i) {
                if (i != 0) {
                    label << ", ";
                }
                const ASTNodeTypeDecl::FieldDecl *field_decl =
                    (i < decl.field_decls.size()) ? &decl.field_decls[i]
                                                  : nullptr;
                label << decl.fields[i];
                if (field_decl != nullptr && !field_decl->type_name.empty()) {
                    label << ": " << field_decl->type_name;
                    if (field_decl->is_list) {
                        label << "[]";
                    }
                }
            }
            label << ")";
        } else if (!decl.is_abstract) {
            label << "()";
        }
        for (const ASTNodeTypeDecl::VirtualMethodDecl &method :
             decl.virtual_methods) {
            label << "\\nvirtual " << method.name << "() -> "
                  << method.return_type_name;
        }
        for (const ASTNodeTypeDecl::VirtualMethodImplDecl &impl :
             decl.virtual_method_impls) {
            label << "\\noverride " << impl.name << "() -> "
                  << impl.return_type_name;
        }
        out << "  a" << next_id << " [label=\""
            << compiler::common::EscapeGraphvizLabel(label.str()) << "\"];\n";
        out << "  astdecls -> a" << next_id << ";\n";
        ++next_id;
    }

    out << "  rules [label=\"Rules\"];\n";
    out << "  root -> rules;\n";
    for (const RuleDefinition &rule : spec.rules) {
        const std::size_t rule_id = next_id++;
        out << "  r" << rule_id << " [label=\"rule "
            << compiler::common::EscapeGraphvizLabel(rule.lhs) << "\"];\n";
        out << "  rules -> r" << rule_id << ";\n";
        for (const RuleAlternative &alt : rule.alternatives) {
            std::ostringstream label;
            label << rule.lhs << " -> ";
            for (std::size_t i = 0; i < alt.symbols.size(); ++i) {
                if (i != 0) {
                    label << ' ';
                }
                label << alt.symbols[i];
            }
            if (alt.action.kind != RuleActionKind::None) {
                label << "\\n=> " << RuleActionToString(alt.action);
            }
            const std::size_t alt_id = next_id++;
            out << "  ralt" << alt_id << " [label=\""
                << compiler::common::EscapeGraphvizLabel(label.str())
                << "\"];\n";
            out << "  r" << rule_id << " -> ralt" << alt_id << ";\n";
        }
    }

    out << "}\n";
    return out.str();
}

std::string
LR1CanonicalCollectionToGraphvizDot(const LR1CanonicalCollection &collection,
                                    std::string_view graph_name) {
    return compiler::parsergen1::LR1CanonicalCollectionToGraphvizDot(
        collection, graph_name);
}

std::string LR1ParseTableToGraphvizDot(const LR1ParseTable &table,
                                       std::string_view graph_name) {
    return compiler::parsergen1::LR1ParseTableToGraphvizDot(table, graph_name);
}

GeneratedAST BuildGeneratedASTFromCST(const CST &cst,
                                      const LR1ParseTable &parse_table,
                                      const Stage2SpecAST &spec) {
    ValidateStage2Spec(spec);
    if (cst.Empty()) {
        throw BuildException("cannot build AST from empty CST");
    }

    GeneratedAST out;
    out.root = BuildGeneratedASTNodeRecursive(cst.Root(), parse_table, spec);
    if (out.root == nullptr) {
        throw BuildException("top-level action did not produce an AST node");
    }
    return out;
}

std::string GeneratedASTToGraphvizDot(const GeneratedAST &ast,
                                      std::string_view graph_name) {
    if (ast.Empty()) {
        throw BuildException("cannot render empty generated AST");
    }

    std::ostringstream out;
    out << "digraph " << compiler::common::SanitizeIdentifier(graph_name, "ast")
        << " {\n";
    out << "  rankdir=TB;\n";
    out << "  node [shape=box];\n";
    out << "  __root [shape=point];\n";
    std::size_t next_id = 0;
    const std::size_t root_id =
        EmitGeneratedASTGraphvizNode(ast.Root(), out, next_id);
    out << "  __root -> n" << root_id << ";\n";
    out << "}\n";
    return out.str();
}

} // namespace compiler::parsergen
