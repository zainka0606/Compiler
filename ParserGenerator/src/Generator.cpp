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

std::string AstBaseClassName(const ASTNodeTypeDecl& decl) {
    return decl.base_type_name.empty() ? "GeneratedNodeBase" : decl.base_type_name;
}

std::string AstFieldStorageType(const ASTNodeTypeDecl::FieldDecl& field) {
    if (field.type_name.empty()) {
        return "std::unique_ptr<GeneratedNodeBase>";
    }
    if (IsBuiltinTextType(field.type_name)) {
        return "std::string";
    }
    return "std::unique_ptr<" + field.type_name + ">";
}

std::string AstFieldGetterConstReturnType(const ASTNodeTypeDecl::FieldDecl& field) {
    if (field.type_name.empty()) {
        return "const GeneratedNodeBase&";
    }
    if (IsBuiltinTextType(field.type_name)) {
        return "const std::string&";
    }
    return "const " + field.type_name + "&";
}

std::string AstFieldGetterMutableReturnType(const ASTNodeTypeDecl::FieldDecl& field) {
    if (field.type_name.empty()) {
        return "GeneratedNodeBase&";
    }
    if (IsBuiltinTextType(field.type_name)) {
        return "std::string&";
    }
    return field.type_name + "&";
}

bool SupportsTypedAstCodegen(const Stage2SpecAST& spec, std::string* reason) {
    for (const ASTNodeTypeDecl& decl : spec.ast_node_types) {
        if (decl.field_decls.size() != decl.fields.size()) {
            if (reason) {
                *reason = "field metadata mismatch in AST declaration '" + decl.name + "'";
            }
            return false;
        }
        for (const ASTNodeTypeDecl::FieldDecl& field : decl.field_decls) {
            if (field.type_name.empty()) {
                if (reason) {
                    *reason = "AST field '" + decl.name + "." + field.name + "' is missing an explicit type";
                }
                return false;
            }
        }
    }
    return true;
}

void EmitTypedAstHeader(std::ostringstream& h, const Stage2SpecAST& spec) {
    h << "namespace ast {\n\n";
    h << "class GeneratedNodeBase;\n";
    h << "struct ChildFieldView { std::string_view name; const GeneratedNodeBase* value; };\n";
    h << "struct TextFieldView { std::string_view name; std::string_view value; };\n\n";
    h << "class GeneratedNodeBase {\n";
    h << "public:\n";
    h << "    virtual ~GeneratedNodeBase() = default;\n";
    h << "    [[nodiscard]] virtual std::string_view KindName() const = 0;\n";
    h << "    [[nodiscard]] virtual std::vector<ChildFieldView> ChildFields() const = 0;\n";
    h << "    [[nodiscard]] virtual std::vector<TextFieldView> TextFields() const = 0;\n";
    h << "};\n\n";

    for (const ASTNodeTypeDecl& decl : spec.ast_node_types) {
        h << "class " << decl.name << ";\n";
    }
    h << "\n";

    for (const ASTNodeTypeDecl& decl : spec.ast_node_types) {
        const std::string base_name = AstBaseClassName(decl);
        h << "class " << decl.name;
        if (!decl.is_abstract) {
            h << " final";
        }
        h << " : public " << base_name << " {\n";
        h << "public:\n";
        h << "    ~" << decl.name << "() override = default;\n";

        if (decl.is_abstract) {
            for (const ASTNodeTypeDecl::VirtualMethodDecl& method : decl.virtual_methods) {
                h << "    [[nodiscard]] virtual " << MapVirtualReturnType(method.return_type_name) << " " << method.name
                  << "() const { throw std::runtime_error(\"virtual method "
                  << compiler::common::EscapeForCppString(decl.name + "::" + method.name)
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
            h << AstFieldStorageType(decl.field_decls[i]) << " " << decl.field_decls[i].name;
        }
        h << ");\n";
        h << "    [[nodiscard]] std::string_view KindName() const override;\n";
        h << "    [[nodiscard]] std::vector<ChildFieldView> ChildFields() const override;\n";
        h << "    [[nodiscard]] std::vector<TextFieldView> TextFields() const override;\n";

        for (const ASTNodeTypeDecl::FieldDecl& field : decl.field_decls) {
            h << "    [[nodiscard]] " << AstFieldGetterConstReturnType(field) << " " << field.name << "() const;\n";
            h << "    [[nodiscard]] " << AstFieldGetterMutableReturnType(field) << " " << field.name << "();\n";
        }
        for (const ASTNodeTypeDecl::VirtualMethodImplDecl& method_impl : decl.virtual_method_impls) {
            h << "    [[nodiscard]] " << MapVirtualReturnType(method_impl.return_type_name) << " " << method_impl.name
              << "() const override;\n";
        }

        h << "private:\n";
        for (const ASTNodeTypeDecl::FieldDecl& field : decl.field_decls) {
            h << "    " << AstFieldStorageType(field) << " " << field.name << "_;\n";
        }
        h << "};\n\n";
    }

    h << "struct AST {\n";
    h << "    std::unique_ptr<GeneratedNodeBase> root;\n";
    h << "    compiler::parsergen::GeneratedAST backing;\n\n";
    h << "    [[nodiscard]] bool Empty() const { return root == nullptr; }\n";
    h << "    [[nodiscard]] const GeneratedNodeBase& Root() const { if (!root) throw std::runtime_error(\"AST root is empty\"); return *root; }\n";
    h << "    [[nodiscard]] GeneratedNodeBase& Root() { if (!root) throw std::runtime_error(\"AST root is empty\"); return *root; }\n";
    h << "};\n\n";
    h << "} // namespace ast\n\n";
}

void EmitTypedAstSource(std::ostringstream& cc, const Stage2SpecAST& spec) {
    cc << "namespace ast {\n\n";

    for (const ASTNodeTypeDecl& decl : spec.ast_node_types) {
        if (decl.is_abstract) {
            continue;
        }

        cc << decl.name << "::" << decl.name << "(";
        for (std::size_t i = 0; i < decl.field_decls.size(); ++i) {
            if (i != 0) {
                cc << ", ";
            }
            cc << AstFieldStorageType(decl.field_decls[i]) << " " << decl.field_decls[i].name;
        }
        cc << ")";
        if (!decl.field_decls.empty()) {
            cc << " : ";
            for (std::size_t i = 0; i < decl.field_decls.size(); ++i) {
                if (i != 0) {
                    cc << ", ";
                }
                const auto& field = decl.field_decls[i];
                cc << field.name << "_(std::move(" << field.name << "))";
            }
        }
        cc << " {}\n\n";

        cc << "std::string_view " << decl.name << "::KindName() const {\n";
        cc << "    return \"" << compiler::common::EscapeForCppString(decl.name) << "\";\n";
        cc << "}\n\n";

        cc << "std::vector<ChildFieldView> " << decl.name << "::ChildFields() const {\n";
        cc << "    std::vector<ChildFieldView> out;\n";
        for (const auto& field : decl.field_decls) {
            if (IsBuiltinTextType(field.type_name)) {
                continue;
            }
            cc << "    out.push_back(ChildFieldView{\"" << compiler::common::EscapeForCppString(field.name)
               << "\", " << field.name << "_.get()});\n";
        }
        cc << "    return out;\n";
        cc << "}\n\n";

        cc << "std::vector<TextFieldView> " << decl.name << "::TextFields() const {\n";
        cc << "    std::vector<TextFieldView> out;\n";
        for (const auto& field : decl.field_decls) {
            if (!IsBuiltinTextType(field.type_name)) {
                continue;
            }
            cc << "    out.push_back(TextFieldView{\"" << compiler::common::EscapeForCppString(field.name)
               << "\", " << field.name << "_});\n";
        }
        cc << "    return out;\n";
        cc << "}\n\n";

        for (const auto& field : decl.field_decls) {
            cc << AstFieldGetterConstReturnType(field) << " " << decl.name << "::" << field.name << "() const {\n";
            if (IsBuiltinTextType(field.type_name)) {
                cc << "    return " << field.name << "_;\n";
            } else {
                cc << "    if (!" << field.name << "_) throw std::runtime_error(\"AST child field is null\");\n";
                cc << "    return *" << field.name << "_;\n";
            }
            cc << "}\n\n";

            cc << AstFieldGetterMutableReturnType(field) << " " << decl.name << "::" << field.name << "() {\n";
            if (IsBuiltinTextType(field.type_name)) {
                cc << "    return " << field.name << "_;\n";
            } else {
                cc << "    if (!" << field.name << "_) throw std::runtime_error(\"AST child field is null\");\n";
                cc << "    return *" << field.name << "_;\n";
            }
            cc << "}\n\n";
        }

        for (const ASTNodeTypeDecl::VirtualMethodImplDecl& method_impl : decl.virtual_method_impls) {
            cc << MapVirtualReturnType(method_impl.return_type_name) << " " << decl.name << "::" << method_impl.name
               << "() const {\n";
            cc << method_impl.body_cpp;
            if (!method_impl.body_cpp.empty() && method_impl.body_cpp.back() != '\n') {
                cc << "\n";
            }
            cc << "}\n\n";
        }
    }

    cc << "const compiler::parsergen::GeneratedASTNodeChildField* FindChildField(const compiler::parsergen::GeneratedASTNode& node, std::string_view name) {\n";
    cc << "    for (const auto& field : node.ChildFields()) {\n";
    cc << "        if (field.name == name) return &field;\n";
    cc << "    }\n";
    cc << "    return nullptr;\n";
    cc << "}\n\n";

    cc << "const compiler::parsergen::GeneratedASTNodeTextField* FindTextField(const compiler::parsergen::GeneratedASTNode& node, std::string_view name) {\n";
    cc << "    for (const auto& field : node.TextFields()) {\n";
    cc << "        if (field.name == name) return &field;\n";
    cc << "    }\n";
    cc << "    return nullptr;\n";
    cc << "}\n\n";

    cc << "std::unique_ptr<GeneratedNodeBase> ConvertNode(const compiler::parsergen::GeneratedASTNode& node);\n\n";
    cc << "template <typename T>\n";
    cc << "std::unique_ptr<T> ConvertNodeAs(const compiler::parsergen::GeneratedASTNode& node) {\n";
    cc << "    std::unique_ptr<GeneratedNodeBase> converted = ConvertNode(node);\n";
    cc << "    auto* raw = dynamic_cast<T*>(converted.release());\n";
    cc << "    if (raw == nullptr) {\n";
    cc << "        throw compiler::parsergen::BuildException(\"generated AST node type mismatch during typed AST conversion\");\n";
    cc << "    }\n";
    cc << "    return std::unique_ptr<T>(raw);\n";
    cc << "}\n\n";

    cc << "std::unique_ptr<GeneratedNodeBase> ConvertNode(const compiler::parsergen::GeneratedASTNode& node) {\n";
    for (const ASTNodeTypeDecl& decl : spec.ast_node_types) {
        if (decl.is_abstract) {
            continue;
        }
        cc << "    if (node.KindName() == \"" << compiler::common::EscapeForCppString(decl.name) << "\") {\n";
        cc << "        return std::make_unique<" << decl.name << ">(";
        for (std::size_t i = 0; i < decl.field_decls.size(); ++i) {
            if (i != 0) {
                cc << ", ";
            }
            const auto& field = decl.field_decls[i];
            if (IsBuiltinTextType(field.type_name)) {
                cc << "[&]() -> std::string { const auto* f = FindTextField(node, \""
                   << compiler::common::EscapeForCppString(field.name)
                   << "\"); if (f == nullptr) throw compiler::parsergen::BuildException(\"missing text field in generated AST\"); return f->value; }()";
            } else {
                const std::string target_type = field.type_name.empty() ? "GeneratedNodeBase" : field.type_name;
                cc << "[&]() -> std::unique_ptr<" << target_type << "> { const auto* f = FindChildField(node, \""
                   << compiler::common::EscapeForCppString(field.name)
                   << "\"); if (f == nullptr || !f->value) throw compiler::parsergen::BuildException(\"missing child field in generated AST\"); return ConvertNodeAs<"
                   << target_type << ">(*f->value); }()";
            }
        }
        cc << ");\n";
        cc << "    }\n";
    }
    cc << "    throw compiler::parsergen::BuildException(std::string(\"unknown generated AST node kind: \") + std::string(node.KindName()));\n";
    cc << "}\n\n";

    cc << "} // namespace ast\n\n";
}

} // namespace

GeneratedParserFiles GenerateCppParser(const Stage2SpecAST& spec,
                                       std::string_view stage2_spec_source,
                                       std::string_view header_filename,
                                       std::string_view source_filename) {
    ValidateStage2Spec(spec);

    GeneratedParserFiles out;
    out.namespace_name = compiler::common::SanitizeIdentifier(spec.grammar_name, "Generated");
    out.parser_class_name = out.namespace_name + "Parser";
    out.header_filename = header_filename.empty() ? (out.parser_class_name + ".h") : std::string(header_filename);
    out.source_filename = source_filename.empty() ? (out.parser_class_name + ".cpp") : std::string(source_filename);

    const std::string embedded_spec_literal = MakeRawStringLiteral(stage2_spec_source);
    const std::string grammar_name_escaped = compiler::common::EscapeForCppString(spec.grammar_name);

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
        h << "    [[nodiscard]] CST Parse(const std::vector<Token>& tokens) const;\n";
        h << "    [[nodiscard]] AST ParseToAST(const std::vector<Token>& tokens) const;\n";
        h << "    [[nodiscard]] static std::string CSTToGraphvizDot(const CST& cst, std::string_view graph_name = \"cst\");\n";
        h << "    [[nodiscard]] static std::string ASTToGraphvizDot(const AST& ast, std::string_view graph_name = \"ast\");\n";
        h << "    [[nodiscard]] static const compiler::parsergen::Stage2SpecAST& Spec();\n";
        h << "    [[nodiscard]] static const compiler::parsergen::LR1ParseTable& ParseTable();\n";
        h << "    [[nodiscard]] static std::string_view GrammarName();\n";
        h << "};\n\n";
        h << "} // namespace generated::" << out.namespace_name << "\n";
        out.header_source = h.str();
    }

    {
        std::ostringstream cc;
        cc << "#include \"" << out.header_filename << "\"\n\n";
        cc << "#include <cmath>\n";
        cc << "#include <cstdlib>\n";
        cc << "#include <memory>\n";
        cc << "#include <string>\n";
        cc << "#include <string_view>\n";
        cc << "#include <utility>\n";
        cc << "#include <vector>\n\n";
        cc << "namespace generated::" << out.namespace_name << " {\n\n";
        cc << "namespace {\n";
        cc << "constexpr const char* kEmbeddedStage2Spec = " << embedded_spec_literal << ";\n";
        if (!emit_typed_ast) {
            cc << "constexpr const char* kUntypedAstFallbackReason = \"" << compiler::common::EscapeForCppString(typed_reason)
               << "\";\n";
        }
        cc << "} // namespace\n\n";

        if (emit_typed_ast) {
            EmitTypedAstSource(cc, spec);
        }

        cc << "const compiler::parsergen::Stage2SpecAST& " << out.parser_class_name << "::Spec() {\n";
        cc << "    static const compiler::parsergen::Stage2SpecAST spec = compiler::parsergen::ParseStage2Spec(kEmbeddedStage2Spec);\n";
        cc << "    return spec;\n";
        cc << "}\n\n";

        cc << "const compiler::parsergen::LR1ParseTable& " << out.parser_class_name << "::ParseTable() {\n";
        cc << "    static const compiler::parsergen::LR1ParseTable table = []() {\n";
        cc << "        compiler::parsergen::LR1ParseTable built = compiler::parsergen::BuildLR1ParseTable(Spec());\n";
        cc << "        if (!built.conflicts.empty()) {\n";
        cc << "            throw compiler::parsergen::BuildException(\"generated parser grammar has \" + std::to_string(built.conflicts.size()) + \" conflict(s)\");\n";
        cc << "        }\n";
        cc << "        return built;\n";
        cc << "    }();\n";
        cc << "    return table;\n";
        cc << "}\n\n";

        cc << "std::string_view " << out.parser_class_name << "::GrammarName() {\n";
        cc << "    return \"" << grammar_name_escaped << "\";\n";
        cc << "}\n\n";

        cc << out.parser_class_name << "::CST " << out.parser_class_name << "::Parse(const std::vector<Token>& tokens) const {\n";
        cc << "    return compiler::parsergen::ParseTokensToCST(ParseTable(), tokens);\n";
        cc << "}\n\n";

        cc << out.parser_class_name << "::AST " << out.parser_class_name << "::ParseToAST(const std::vector<Token>& tokens) const {\n";
        cc << "    CST cst = Parse(tokens);\n";
        if (emit_typed_ast) {
            cc << "    AST out;\n";
            cc << "    out.backing = compiler::parsergen::BuildGeneratedASTFromCST(cst, ParseTable(), Spec());\n";
            cc << "    out.root = ast::ConvertNode(out.backing.Root());\n";
            cc << "    if (!out.root) { throw compiler::parsergen::BuildException(\"generated AST conversion produced empty root\"); }\n";
            cc << "    return out;\n";
        } else {
            cc << "    (void) kUntypedAstFallbackReason;\n";
            cc << "    return compiler::parsergen::BuildGeneratedASTFromCST(cst, ParseTable(), Spec());\n";
        }
        cc << "}\n\n";

        cc << "std::string " << out.parser_class_name << "::CSTToGraphvizDot(const CST& cst, std::string_view graph_name) {\n";
        cc << "    return compiler::parsergen::CSTToGraphvizDot(cst, graph_name);\n";
        cc << "}\n\n";

        cc << "std::string " << out.parser_class_name << "::ASTToGraphvizDot(const AST& ast, std::string_view graph_name) {\n";
        if (emit_typed_ast) {
            cc << "    if (ast.backing.Empty()) { throw compiler::parsergen::BuildException(\"cannot render empty AST backing graph\"); }\n";
            cc << "    return compiler::parsergen::GeneratedASTToGraphvizDot(ast.backing, graph_name);\n";
        } else {
            cc << "    return compiler::parsergen::GeneratedASTToGraphvizDot(ast, graph_name);\n";
        }
        cc << "}\n\n";

        cc << "} // namespace generated::" << out.namespace_name << "\n";
        out.implementation_source = cc.str();
    }

    return out;
}

} // namespace compiler::parsergen
