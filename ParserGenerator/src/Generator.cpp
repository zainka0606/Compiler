#include "ParserGenerator.h"

#include "Common/Identifier.h"
#include "Common/StringEscape.h"

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace compiler::parsergen {

namespace {

std::string MakeRawStringLiteral(std::string_view text) {
    std::string delimiter = "PG2SPEC";
    std::size_t suffix = 0;
    while (true) {
        const std::string closing = ")" + delimiter + "\"";
        if (text.find(closing) == std::string_view::npos) {
            break;
        }
        ++suffix;
        delimiter = "PG2SPEC_" + std::to_string(suffix);
    }
    return "R\"" + delimiter + "(" + std::string(text) + ")" + delimiter + "\"";
}

bool IsBuiltinTextType(std::string_view type_name) {
    return type_name == "string" || type_name == "text";
}

std::string MapVirtualReturnType(std::string_view type_name) {
    if (IsBuiltinTextType(type_name)) {
        return "std::string";
    }
    return std::string(type_name);
}

std::string AstBaseClassName(const ASTNodeTypeDecl &decl) {
    return decl.base_type_name.empty() ? "GeneratedNodeBase"
                                       : decl.base_type_name;
}

std::string AstFieldStorageType(const ASTNodeTypeDecl::FieldDecl &field) {
    if (field.is_list) {
        if (field.type_name.empty()) {
            return "std::vector<std::unique_ptr<GeneratedNodeBase>>";
        }
        if (IsBuiltinTextType(field.type_name)) {
            return "std::vector<std::string>";
        }
        return "std::vector<std::unique_ptr<" + field.type_name + ">>";
    }
    if (field.type_name.empty()) {
        return "std::unique_ptr<GeneratedNodeBase>";
    }
    if (IsBuiltinTextType(field.type_name)) {
        return "std::string";
    }
    return "std::unique_ptr<" + field.type_name + ">";
}

std::string
AstFieldGetterConstReturnType(const ASTNodeTypeDecl::FieldDecl &field) {
    if (field.is_list) {
        if (field.type_name.empty()) {
            return "const std::vector<std::unique_ptr<GeneratedNodeBase>>&";
        }
        if (IsBuiltinTextType(field.type_name)) {
            return "const std::vector<std::string>&";
        }
        return "const std::vector<std::unique_ptr<" + field.type_name + ">>&";
    }
    if (field.type_name.empty()) {
        return "const GeneratedNodeBase&";
    }
    if (IsBuiltinTextType(field.type_name)) {
        return "const std::string&";
    }
    return "const " + field.type_name + "&";
}

std::string
AstFieldGetterMutableReturnType(const ASTNodeTypeDecl::FieldDecl &field) {
    if (field.is_list) {
        if (field.type_name.empty()) {
            return "std::vector<std::unique_ptr<GeneratedNodeBase>>&";
        }
        if (IsBuiltinTextType(field.type_name)) {
            return "std::vector<std::string>&";
        }
        return "std::vector<std::unique_ptr<" + field.type_name + ">>&";
    }
    if (field.type_name.empty()) {
        return "GeneratedNodeBase&";
    }
    if (IsBuiltinTextType(field.type_name)) {
        return "std::string&";
    }
    return field.type_name + "&";
}

bool SupportsTypedAstCodegen(const Stage2SpecAST &spec, std::string *reason) {
    for (const ASTNodeTypeDecl &decl : spec.ast_node_types) {
        if (decl.field_decls.size() != decl.fields.size()) {
            if (reason) {
                *reason = "field metadata mismatch in AST declaration '" +
                          decl.name + "'";
            }
            return false;
        }
        for (const ASTNodeTypeDecl::FieldDecl &field : decl.field_decls) {
            if (field.type_name.empty()) {
                if (reason) {
                    *reason = "AST field '" + decl.name + "." + field.name +
                              "' is missing an explicit type";
                }
                return false;
            }
        }
    }
    return true;
}

void EmitTypedAstHeader(std::ostringstream &h, const Stage2SpecAST &spec) {
    h << "namespace ast {\n\n";
    h << "class GeneratedNodeBase;\n";
    h << "struct ChildFieldView { std::string_view name; const "
         "GeneratedNodeBase* value; };\n";
    h << "struct TextFieldView { std::string_view name; std::string_view "
         "value; };\n\n";
    h << "class GeneratedNodeBase {\n";
    h << "public:\n";
    h << "    virtual ~GeneratedNodeBase() = default;\n";
    h << "    [[nodiscard]] virtual std::string_view KindName() const = 0;\n";
    h << "    [[nodiscard]] virtual std::vector<ChildFieldView> ChildFields() "
         "const = 0;\n";
    h << "    [[nodiscard]] virtual std::vector<TextFieldView> TextFields() "
         "const = 0;\n";
    h << "};\n\n";

    for (const ASTNodeTypeDecl &decl : spec.ast_node_types) {
        h << "class " << decl.name << ";\n";
    }
    h << "\n";

    for (const ASTNodeTypeDecl &decl : spec.ast_node_types) {
        const std::string base_name = AstBaseClassName(decl);
        h << "class " << decl.name;
        if (!decl.is_abstract) {
            h << " final";
        }
        h << " : public " << base_name << " {\n";
        h << "public:\n";
        h << "    ~" << decl.name << "() override = default;\n";

        if (decl.is_abstract) {
            for (const ASTNodeTypeDecl::VirtualMethodDecl &method :
                 decl.virtual_methods) {
                h << "    [[nodiscard]] virtual "
                  << MapVirtualReturnType(method.return_type_name) << " "
                  << method.name
                  << "() const { throw std::runtime_error(\"virtual method "
                  << compiler::common::EscapeForCppString(decl.name +
                                                          "::" + method.name)
                  << " not implemented\"); }\n";
            }
            h << "};\n\n";
            continue;
        }

        h << "    explicit " << decl.name << "(";
        for (std::size_t i = 0; i < decl.field_decls.size(); ++i) {
            if (i != 0) {
                h << ", ";
            }
            h << AstFieldStorageType(decl.field_decls[i]) << " "
              << decl.field_decls[i].name;
        }
        h << ");\n";
        h << "    [[nodiscard]] std::string_view KindName() const override;\n";
        h << "    [[nodiscard]] std::vector<ChildFieldView> ChildFields() "
             "const override;\n";
        h << "    [[nodiscard]] std::vector<TextFieldView> TextFields() const "
             "override;\n";

        for (const ASTNodeTypeDecl::FieldDecl &field : decl.field_decls) {
            h << "    [[nodiscard]] " << AstFieldGetterConstReturnType(field)
              << " " << field.name << "() const;\n";
            h << "    [[nodiscard]] " << AstFieldGetterMutableReturnType(field)
              << " " << field.name << "();\n";
        }
        for (const ASTNodeTypeDecl::VirtualMethodImplDecl &method_impl :
             decl.virtual_method_impls) {
            h << "    [[nodiscard]] "
              << MapVirtualReturnType(method_impl.return_type_name) << " "
              << method_impl.name << "() const override;\n";
        }

        h << "private:\n";
        for (const ASTNodeTypeDecl::FieldDecl &field : decl.field_decls) {
            h << "    " << AstFieldStorageType(field) << " " << field.name
              << "_;\n";
        }
        h << "};\n\n";
    }

    h << "struct AST {\n";
    h << "    std::unique_ptr<GeneratedNodeBase> root;\n";
    h << "    compiler::parsergen::GeneratedAST backing;\n\n";
    h << "    [[nodiscard]] bool Empty() const { return root == nullptr; }\n";
    h << "    [[nodiscard]] const GeneratedNodeBase& Root() const { if (!root) "
         "throw std::runtime_error(\"AST root is empty\"); return *root; }\n";
    h << "    [[nodiscard]] GeneratedNodeBase& Root() { if (!root) throw "
         "std::runtime_error(\"AST root is empty\"); return *root; }\n";
    h << "};\n\n";
    h << "} // namespace ast\n\n";
}

void EmitTypedAstSource(std::ostringstream &cc, const Stage2SpecAST &spec,
                        std::string_view parser_class_name) {
    cc << "namespace ast {\n\n";

    for (const ASTNodeTypeDecl &decl : spec.ast_node_types) {
        if (decl.is_abstract) {
            continue;
        }

        cc << decl.name << "::" << decl.name << "(";
        for (std::size_t i = 0; i < decl.field_decls.size(); ++i) {
            if (i != 0) {
                cc << ", ";
            }
            cc << AstFieldStorageType(decl.field_decls[i]) << " "
               << decl.field_decls[i].name;
        }
        cc << ")";
        if (!decl.field_decls.empty()) {
            cc << " : ";
            for (std::size_t i = 0; i < decl.field_decls.size(); ++i) {
                if (i != 0) {
                    cc << ", ";
                }
                const auto &field = decl.field_decls[i];
                cc << field.name << "_(std::move(" << field.name << "))";
            }
        }
        cc << " {}\n\n";

        cc << "std::string_view " << decl.name << "::KindName() const {\n";
        cc << "    return \"" << compiler::common::EscapeForCppString(decl.name)
           << "\";\n";
        cc << "}\n\n";

        cc << "std::vector<ChildFieldView> " << decl.name
           << "::ChildFields() const {\n";
        cc << "    std::vector<ChildFieldView> out;\n";
        for (const auto &field : decl.field_decls) {
            if (IsBuiltinTextType(field.type_name)) {
                continue;
            }
            if (field.is_list) {
                cc << "    for (const auto& value : " << field.name << "_) {\n";
                cc << "        out.push_back(ChildFieldView{\""
                   << compiler::common::EscapeForCppString(field.name)
                   << "\", value.get()});\n";
                cc << "    }\n";
            } else {
                cc << "    out.push_back(ChildFieldView{\""
                   << compiler::common::EscapeForCppString(field.name) << "\", "
                   << field.name << "_.get()});\n";
            }
        }
        cc << "    return out;\n";
        cc << "}\n\n";

        cc << "std::vector<TextFieldView> " << decl.name
           << "::TextFields() const {\n";
        cc << "    std::vector<TextFieldView> out;\n";
        for (const auto &field : decl.field_decls) {
            if (!IsBuiltinTextType(field.type_name)) {
                continue;
            }
            if (field.is_list) {
                cc << "    for (const auto& value : " << field.name << "_) {\n";
                cc << "        out.push_back(TextFieldView{\""
                   << compiler::common::EscapeForCppString(field.name)
                   << "\", value});\n";
                cc << "    }\n";
            } else {
                cc << "    out.push_back(TextFieldView{\""
                   << compiler::common::EscapeForCppString(field.name) << "\", "
                   << field.name << "_});\n";
            }
        }
        cc << "    return out;\n";
        cc << "}\n\n";

        for (const auto &field : decl.field_decls) {
            cc << AstFieldGetterConstReturnType(field) << " " << decl.name
               << "::" << field.name << "() const {\n";
            if (field.is_list || IsBuiltinTextType(field.type_name)) {
                cc << "    return " << field.name << "_;\n";
            } else {
                cc << "    if (!" << field.name
                   << "_) throw std::runtime_error(\"AST child field is "
                      "null\");\n";
                cc << "    return *" << field.name << "_;\n";
            }
            cc << "}\n\n";

            cc << AstFieldGetterMutableReturnType(field) << " " << decl.name
               << "::" << field.name << "() {\n";
            if (field.is_list || IsBuiltinTextType(field.type_name)) {
                cc << "    return " << field.name << "_;\n";
            } else {
                cc << "    if (!" << field.name
                   << "_) throw std::runtime_error(\"AST child field is "
                      "null\");\n";
                cc << "    return *" << field.name << "_;\n";
            }
            cc << "}\n\n";
        }

        for (const ASTNodeTypeDecl::VirtualMethodImplDecl &method_impl :
             decl.virtual_method_impls) {
            cc << MapVirtualReturnType(method_impl.return_type_name) << " "
               << decl.name << "::" << method_impl.name << "() const {\n";
            cc << method_impl.body_cpp;
            if (!method_impl.body_cpp.empty() &&
                method_impl.body_cpp.back() != '\n') {
                cc << "\n";
            }
            cc << "}\n\n";
        }
    }

    cc << "std::string TakeLexemeChild(const compiler::parsergen::CSTNode& "
          "reduction_node, std::size_t rhs_index) {\n";
    cc << "    if (rhs_index == 0 || rhs_index > reduction_node.ChildCount()) "
          "{\n";
    cc << "        throw compiler::parsergen::BuildException(\"RHS index out "
          "of range while taking lexeme child\");\n";
    cc << "    }\n";
    cc << "    const compiler::parsergen::CSTNode& child = "
          "reduction_node.Child(rhs_index - 1);\n";
    cc << "    if (!child.IsTerminal()) {\n";
    cc << "        throw compiler::parsergen::BuildException(\"expected "
          "terminal child for .lexeme action argument\");\n";
    cc << "    }\n";
    cc << "    return std::string(child.Lexeme());\n";
    cc << "}\n\n";

    cc << "std::unique_ptr<GeneratedNodeBase> ConvertNode(const "
          "compiler::parsergen::CSTNode& node);\n\n";
    cc << "template <typename T>\n";
    cc << "std::unique_ptr<T> ConvertNodeAs(const "
          "compiler::parsergen::CSTNode& node) {\n";
    cc << "    std::unique_ptr<GeneratedNodeBase> converted = "
          "ConvertNode(node);\n";
    cc << "    auto* raw = dynamic_cast<T*>(converted.release());\n";
    cc << "    if (raw == nullptr) {\n";
    cc << "        throw compiler::parsergen::BuildException(\"typed AST "
          "conversion produced unexpected node type\");\n";
    cc << "    }\n";
    cc << "    return std::unique_ptr<T>(raw);\n";
    cc << "}\n\n";

    cc << "template <typename T>\n";
    cc << "std::unique_ptr<T> TakeASTChildAs(const "
          "compiler::parsergen::CSTNode& reduction_node, std::size_t "
          "rhs_index) {\n";
    cc << "    if (rhs_index == 0 || rhs_index > reduction_node.ChildCount()) "
          "{\n";
    cc << "        throw compiler::parsergen::BuildException(\"RHS index out "
          "of range while taking AST child\");\n";
    cc << "    }\n";
    cc << "    return ConvertNodeAs<T>(reduction_node.Child(rhs_index - 1));\n";
    cc << "}\n\n";

    cc << "std::unique_ptr<GeneratedNodeBase> ConvertNode(const "
          "compiler::parsergen::CSTNode& node) {\n";
    cc << "    if (node.IsTerminal()) {\n";
    cc << "        throw compiler::parsergen::BuildException(\"cannot convert "
          "terminal CST node to typed AST node\");\n";
    cc << "    }\n";
    cc << "    const compiler::parsergen::FlattenedProduction& production =\n";
    cc << "        compiler::parsergen::GetCSTReductionProduction("
       << parser_class_name << "::ParseTable(), node);\n";
    for (std::size_t rule_index = 0; rule_index < spec.rules.size();
         ++rule_index) {
        const RuleDefinition &rule = spec.rules[rule_index];
        for (std::size_t alt_index = 0; alt_index < rule.alternatives.size();
             ++alt_index) {
            const RuleAlternative &alt = rule.alternatives[alt_index];
            const RuleAction &action = alt.action;
            cc << "    if (production.source_rule_index == " << rule_index
               << " && production.source_alternative_index == " << alt_index
               << ") {\n";
            if (action.kind == RuleActionKind::None) {
                cc << "        throw "
                      "compiler::parsergen::BuildException(\"rule alternative "
                      "has no AST action\");\n";
            } else if (action.kind == RuleActionKind::Pass) {
                cc << "        return ConvertNode(node.Child("
                   << (action.pass_rhs_index - 1) << "));\n";
            } else if (action.kind == RuleActionKind::InlineCpp) {
                cc << action.cpp_code;
                if (!action.cpp_code.empty() &&
                    action.cpp_code.back() != '\n') {
                    cc << "\n";
                }
            } else {
                const ASTNodeTypeDecl *ast_decl = nullptr;
                for (const ASTNodeTypeDecl &decl : spec.ast_node_types) {
                    if (decl.name == action.target_node_name) {
                        ast_decl = &decl;
                        break;
                    }
                }
                if (ast_decl == nullptr) {
                    cc << "        throw "
                          "compiler::parsergen::BuildException(\"unknown AST "
                          "node type in action\");\n";
                } else {
                    cc << "        return std::make_unique<" << ast_decl->name
                       << ">(";
                    for (std::size_t i = 0; i < action.args.size(); ++i) {
                        if (i != 0) {
                            cc << ", ";
                        }
                        const ActionArg &arg = action.args[i];
                        const ASTNodeTypeDecl::FieldDecl &field =
                            ast_decl->field_decls[i];
                        if (field.is_list) {
                            if (IsBuiltinTextType(field.type_name)) {
                                cc << "[&]() -> std::vector<std::string> { "
                                      "std::vector<std::string> values; "
                                      "values.push_back(TakeLexemeChild(node, "
                                   << arg.rhs_index << ")); return values; }()";
                            } else {
                                cc << "[&]() -> std::vector<std::unique_ptr<"
                                   << field.type_name
                                   << ">> { std::vector<std::unique_ptr<"
                                   << field.type_name
                                   << ">> values; "
                                      "values.push_back(TakeASTChildAs<"
                                   << field.type_name << ">(node, "
                                   << arg.rhs_index << ")); return values; }()";
                            }
                        } else if (IsBuiltinTextType(field.type_name)) {
                            cc << "TakeLexemeChild(node, " << arg.rhs_index
                               << ")";
                        } else {
                            cc << "TakeASTChildAs<" << field.type_name
                               << ">(node, " << arg.rhs_index << ")";
                        }
                    }
                    cc << ");\n";
                }
            }
            cc << "    }\n";
        }
    }
    cc << "    throw compiler::parsergen::BuildException(\"unable to match CST "
          "production to typed AST conversion branch\");\n";
    cc << "}\n\n";

    cc << "} // namespace ast\n\n";
}

} // namespace

GeneratedParserFiles GenerateCppParser(const Stage2SpecAST &spec,
                                       std::string_view stage2_spec_source,
                                       std::string_view header_filename,
                                       std::string_view source_filename) {
    ValidateStage2Spec(spec);

    GeneratedParserFiles out;
    out.namespace_name =
        compiler::common::SanitizeIdentifier(spec.grammar_name, "Generated");
    out.parser_class_name = out.namespace_name + "Parser";
    out.header_filename = header_filename.empty()
                              ? (out.parser_class_name + ".h")
                              : std::string(header_filename);
    out.source_filename = source_filename.empty()
                              ? (out.parser_class_name + ".cpp")
                              : std::string(source_filename);

    const std::string embedded_spec_literal =
        MakeRawStringLiteral(stage2_spec_source);
    const std::string grammar_name_escaped =
        compiler::common::EscapeForCppString(spec.grammar_name);

    std::string typed_reason;
    const bool emit_typed_ast = SupportsTypedAstCodegen(spec, &typed_reason);

    {
        std::ostringstream h;
        h << "#pragma once\n\n";
        h << "#include \"ParserGenerator.h\"\n\n";
        h << "#include <memory>\n";
        h << "#include <stdexcept>\n";
        h << "#include <string>\n";
        h << "#include <string_view>\n";
        h << "#include <vector>\n\n";
        h << "namespace generated::" << out.namespace_name << " {\n\n";

        if (emit_typed_ast) {
            EmitTypedAstHeader(h, spec);
        }

        h << "class " << out.parser_class_name << " {\n";
        h << "public:\n";
        h << "    using Token = compiler::parsergen::GenericToken;\n";
        h << "    using CST = compiler::parsergen::CST;\n";
        if (emit_typed_ast) {
            h << "    using AST = ast::AST;\n";
        } else {
            h << "    using AST = compiler::parsergen::GeneratedAST;\n";
        }
        h << "\n";
        h << "    [[nodiscard]] CST Parse(const std::vector<Token>& tokens) "
             "const;\n";
        h << "    [[nodiscard]] AST ParseToAST(const std::vector<Token>& "
             "tokens) const;\n";
        h << "    [[nodiscard]] static std::string CSTToGraphvizDot(const CST& "
             "cst, std::string_view graph_name = \"cst\");\n";
        h << "    [[nodiscard]] static std::string ASTToGraphvizDot(const AST& "
             "ast, std::string_view graph_name = \"ast\");\n";
        h << "    [[nodiscard]] static const "
             "compiler::parsergen::Stage2SpecAST& Spec();\n";
        h << "    [[nodiscard]] static const "
             "compiler::parsergen::LR1ParseTable& ParseTable();\n";
        h << "    [[nodiscard]] static std::string_view GrammarName();\n";
        h << "};\n\n";
        h << "} // namespace generated::" << out.namespace_name << "\n";
        out.header_source = h.str();
    }

    {
        std::ostringstream cc;
        cc << "#include \"" << out.header_filename << "\"\n\n";
        cc << "#include \"Common/Graphviz.h\"\n";
        cc << "#include \"Common/Identifier.h\"\n\n";
        cc << "#include <cmath>\n";
        cc << "#include <cstdlib>\n";
        cc << "#include <memory>\n";
        cc << "#include <sstream>\n";
        cc << "#include <string>\n";
        cc << "#include <string_view>\n";
        cc << "#include <utility>\n";
        cc << "#include <vector>\n\n";
        cc << "namespace generated::" << out.namespace_name << " {\n\n";
        cc << "namespace {\n";
        cc << "constexpr const char* kEmbeddedStage2Spec = "
           << embedded_spec_literal << ";\n";
        if (!emit_typed_ast) {
            cc << "constexpr const char* kUntypedAstFallbackReason = \""
               << compiler::common::EscapeForCppString(typed_reason) << "\";\n";
        }
        cc << "} // namespace\n\n";

        if (emit_typed_ast) {
            EmitTypedAstSource(cc, spec, out.parser_class_name);
        }

        cc << "const compiler::parsergen::Stage2SpecAST& "
           << out.parser_class_name << "::Spec() {\n";
        cc << "    static const compiler::parsergen::Stage2SpecAST spec = "
              "compiler::parsergen::ParseStage2Spec(kEmbeddedStage2Spec);\n";
        cc << "    return spec;\n";
        cc << "}\n\n";

        cc << "const compiler::parsergen::LR1ParseTable& "
           << out.parser_class_name << "::ParseTable() {\n";
        cc << "    static const compiler::parsergen::LR1ParseTable table = "
              "[]() {\n";
        cc << "        compiler::parsergen::LR1ParseTable built = "
              "compiler::parsergen::BuildLR1ParseTable(Spec());\n";
        cc << "        if (!built.conflicts.empty()) {\n";
        cc << "            throw "
              "compiler::parsergen::BuildException(\"generated parser grammar "
              "has \" + std::to_string(built.conflicts.size()) + \" "
              "conflict(s)\");\n";
        cc << "        }\n";
        cc << "        return built;\n";
        cc << "    }();\n";
        cc << "    return table;\n";
        cc << "}\n\n";

        cc << "std::string_view " << out.parser_class_name
           << "::GrammarName() {\n";
        cc << "    return \"" << grammar_name_escaped << "\";\n";
        cc << "}\n\n";

        cc << out.parser_class_name << "::CST " << out.parser_class_name
           << "::Parse(const std::vector<Token>& tokens) const {\n";
        cc << "    return compiler::parsergen::ParseTokensToCST(ParseTable(), "
              "tokens);\n";
        cc << "}\n\n";

        cc << out.parser_class_name << "::AST " << out.parser_class_name
           << "::ParseToAST(const std::vector<Token>& tokens) const {\n";
        cc << "    CST cst = Parse(tokens);\n";
        if (emit_typed_ast) {
            cc << "    AST out;\n";
            cc << "    out.root = ast::ConvertNode(cst.Root());\n";
            cc << "    if (!out.root) { throw "
                  "compiler::parsergen::BuildException(\"generated AST "
                  "conversion produced empty root\"); }\n";
            cc << "    return out;\n";
        } else {
            cc << "    (void) kUntypedAstFallbackReason;\n";
            cc << "    return "
                  "compiler::parsergen::BuildGeneratedASTFromCST(cst, "
                  "ParseTable(), Spec());\n";
        }
        cc << "}\n\n";

        cc << "std::string " << out.parser_class_name
           << "::CSTToGraphvizDot(const CST& cst, std::string_view graph_name) "
              "{\n";
        cc << "    return compiler::parsergen::CSTToGraphvizDot(cst, "
              "graph_name);\n";
        cc << "}\n\n";

        cc << "std::string " << out.parser_class_name
           << "::ASTToGraphvizDot(const AST& ast, std::string_view graph_name) "
              "{\n";
        if (emit_typed_ast) {
            cc << "    if (ast.Empty()) { throw "
                  "compiler::parsergen::BuildException(\"cannot render empty "
                  "AST\"); }\n";
            cc << "    std::ostringstream out;\n";
            cc << "    out << \"digraph \" << "
                  "compiler::common::SanitizeIdentifier(graph_name, \"ast\") "
                  "<< \" {\\n\";\n";
            cc << "    out << \"  rankdir=TB;\\n\";\n";
            cc << "    out << \"  node [shape=box];\\n\";\n";
            cc << "    out << \"  __root [shape=point];\\n\";\n";
            cc << "    std::size_t next_id = 0;\n";
            cc << "    const auto emit_node = [&](const auto& self, const "
                  "ast::GeneratedNodeBase& node_ref, std::ostringstream& "
                  "out_ref,\n";
            cc << "                               std::size_t& next_id_ref) -> "
                  "std::size_t {\n";
            cc << "        const std::size_t id = next_id_ref++;\n";
            cc << "        std::string label(node_ref.KindName());\n";
            cc << "        for (const auto& text_field : "
                  "node_ref.TextFields()) {\n";
            cc << "            label.push_back('\\n');\n";
            cc << "            label += std::string(text_field.name);\n";
            cc << "            label.push_back('=');\n";
            cc << "            label += std::string(text_field.value);\n";
            cc << "        }\n";
            cc << "        out_ref << \"  n\" << id << \" [label=\\\"\" << "
                  "compiler::common::EscapeGraphvizLabel(label) << "
                  "\"\\\"];\\n\";\n";
            cc << "        for (const auto& child_field : "
                  "node_ref.ChildFields()) {\n";
            cc << "            if (child_field.value == nullptr) {\n";
            cc << "                continue;\n";
            cc << "            }\n";
            cc << "            const std::size_t child_id = self(self, "
                  "*child_field.value, out_ref, next_id_ref);\n";
            cc << "            out_ref << \"  n\" << id << \" -> n\" << "
                  "child_id << \" [label=\\\"\"\n";
            cc << "                    << "
                  "compiler::common::EscapeGraphvizLabel(child_field.name) << "
                  "\"\\\"];\\n\";\n";
            cc << "        }\n";
            cc << "        return id;\n";
            cc << "    };\n";
            cc << "    const std::size_t root_id = emit_node(emit_node, "
                  "ast.Root(), out, next_id);\n";
            cc << "    out << \"  __root -> n\" << root_id << \";\\n\";\n";
            cc << "    out << \"}\\n\";\n";
            cc << "    return out.str();\n";
        } else {
            cc << "    return "
                  "compiler::parsergen::GeneratedASTToGraphvizDot(ast, "
                  "graph_name);\n";
        }
        cc << "}\n\n";

        cc << "} // namespace generated::" << out.namespace_name << "\n";
        out.implementation_source = cc.str();
    }

    return out;
}

} // namespace compiler::parsergen
