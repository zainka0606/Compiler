#include "Common/FileIO.h"
#include "Interpreter.h"

#include "GeneratedParser.h"
#include "LanguageLexer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace compiler::interpreter {

namespace {

using GeneratedLexer = detail::LanguageLexer;
using GeneratedToken = detail::Token;
using GeneratedTokenKind = detail::LanguageTokenKind;
using GeneratedParser = generated::MiniLangInterpreter::MiniLangInterpreterParser;
namespace gen = generated::MiniLangInterpreter::ast;

const char* TerminalNameForToken(GeneratedTokenKind kind) {
    switch (kind) {
        case GeneratedTokenKind::KW_FN:
            return "KW_FN";
        case GeneratedTokenKind::KW_CLASS:
            return "KW_CLASS";
        case GeneratedTokenKind::KW_LET:
            return "KW_LET";
        case GeneratedTokenKind::KW_RETURN:
            return "KW_RETURN";
        case GeneratedTokenKind::KW_IF:
            return "KW_IF";
        case GeneratedTokenKind::KW_ELSE:
            return "KW_ELSE";
        case GeneratedTokenKind::KW_FOR:
            return "KW_FOR";
        case GeneratedTokenKind::KW_WHILE:
            return "KW_WHILE";
        case GeneratedTokenKind::KW_SWITCH:
            return "KW_SWITCH";
        case GeneratedTokenKind::KW_CASE:
            return "KW_CASE";
        case GeneratedTokenKind::KW_DEFAULT:
            return "KW_DEFAULT";
        case GeneratedTokenKind::KW_TRUE:
            return "KW_TRUE";
        case GeneratedTokenKind::KW_FALSE:
            return "KW_FALSE";
        case GeneratedTokenKind::ID:
            return "ID";
        case GeneratedTokenKind::FSTRING:
            return "FSTRING";
        case GeneratedTokenKind::STRING:
            return "STRING";
        case GeneratedTokenKind::CHAR:
            return "CHAR";
        case GeneratedTokenKind::NUMBER:
            return "NUMBER";
        case GeneratedTokenKind::SHLEQ:
            return "SHLEQ";
        case GeneratedTokenKind::SHREQ:
            return "SHREQ";
        case GeneratedTokenKind::PLUSEQ:
            return "PLUSEQ";
        case GeneratedTokenKind::MINUSEQ:
            return "MINUSEQ";
        case GeneratedTokenKind::STAREQ:
            return "STAREQ";
        case GeneratedTokenKind::SLASHEQ:
            return "SLASHEQ";
        case GeneratedTokenKind::MODEQ:
            return "MODEQ";
        case GeneratedTokenKind::ANDEQ:
            return "ANDEQ";
        case GeneratedTokenKind::OREQ:
            return "OREQ";
        case GeneratedTokenKind::CARETEQ:
            return "CARETEQ";
        case GeneratedTokenKind::EQEQ:
            return "EQEQ";
        case GeneratedTokenKind::NEQ:
            return "NEQ";
        case GeneratedTokenKind::LAND:
            return "LAND";
        case GeneratedTokenKind::LOR:
            return "LOR";
        case GeneratedTokenKind::SHL:
            return "SHL";
        case GeneratedTokenKind::SHR:
            return "SHR";
        case GeneratedTokenKind::LTE:
            return "LTE";
        case GeneratedTokenKind::GTE:
            return "GTE";
        case GeneratedTokenKind::LT:
            return "LT";
        case GeneratedTokenKind::GT:
            return "GT";
        case GeneratedTokenKind::PLUS:
            return "PLUS";
        case GeneratedTokenKind::MINUS:
            return "MINUS";
        case GeneratedTokenKind::POW:
            return "POW";
        case GeneratedTokenKind::STAR:
            return "STAR";
        case GeneratedTokenKind::SLASH:
            return "SLASH";
        case GeneratedTokenKind::MOD:
            return "MOD";
        case GeneratedTokenKind::AMP:
            return "AMP";
        case GeneratedTokenKind::PIPE:
            return "PIPE";
        case GeneratedTokenKind::BANG:
            return "BANG";
        case GeneratedTokenKind::TILDE:
            return "TILDE";
        case GeneratedTokenKind::CARET:
            return "CARET";
        case GeneratedTokenKind::EQUAL:
            return "EQUAL";
        case GeneratedTokenKind::COMMA:
            return "COMMA";
        case GeneratedTokenKind::SEMI:
            return "SEMI";
        case GeneratedTokenKind::QMARK:
            return "QMARK";
        case GeneratedTokenKind::COLON:
            return "COLON";
        case GeneratedTokenKind::DOT:
            return "DOT";
        case GeneratedTokenKind::LPAREN:
            return "LPAREN";
        case GeneratedTokenKind::RPAREN:
            return "RPAREN";
        case GeneratedTokenKind::LBRACE:
            return "LBRACE";
        case GeneratedTokenKind::RBRACE:
            return "RBRACE";
        case GeneratedTokenKind::LBRACKET:
            return "LBRACKET";
        case GeneratedTokenKind::RBRACKET:
            return "RBRACKET";
        case GeneratedTokenKind::EndOfFile:
            return "$";
    }
    return "<invalid>";
}

std::vector<compiler::parsergen::GenericToken> ToGenericTokens(const std::vector<GeneratedToken>& tokens) {
    std::vector<compiler::parsergen::GenericToken> out;
    out.reserve(tokens.size());
    for (const GeneratedToken& token : tokens) {
        out.push_back(compiler::parsergen::GenericToken{
            .kind = TerminalNameForToken(token.kind),
            .lexeme = std::string(token.lexeme),
            .line = token.line,
            .column = token.column,
        });
    }
    return out;
}

const gen::Program& RequireProgram(const AST& ast) {
    if (ast.Empty()) {
        throw InterpreterException("program AST is empty");
    }
    const auto* program = dynamic_cast<const gen::Program*>(&ast.Root());
    if (program == nullptr) {
        throw InterpreterException("program AST root is not Program");
    }
    return *program;
}

double ParseNumberFromText(std::string_view text, std::string_view context) {
    std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        throw InterpreterException(std::string("invalid number in ") + std::string(context) + ": " + copy);
    }
    return value;
}

std::string DecodeEscapedText(std::string_view text, std::string_view context) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i + 1 >= text.size()) {
            throw InterpreterException(std::string("dangling escape in ") + std::string(context));
        }
        const char next = text[++i];
        switch (next) {
            case 'n':
                out.push_back('\n');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case '0':
                out.push_back('\0');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '\"':
                out.push_back('\"');
                break;
            case '\'':
                out.push_back('\'');
                break;
            default:
                out.push_back(next);
                break;
        }
    }
    return out;
}

std::string ParseQuotedLiteral(std::string_view text, char quote, std::string_view context) {
    if (text.size() < 2 || text.front() != quote || text.back() != quote) {
        throw InterpreterException(std::string("invalid ") + std::string(context) + " literal: " + std::string(text));
    }
    return DecodeEscapedText(text.substr(1, text.size() - 2), context);
}

std::string AsString(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* character = std::get_if<char>(&value)) {
        return std::string(1, *character);
    }
    return ValueToString(value);
}

std::string NumberToString(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

std::string ValueToStringInternal(const Value& value);

std::string ObjectToString(const ObjectInstancePtr& object) {
    if (!object) {
        return "null-object";
    }

    std::ostringstream out;
    out << object->class_name << "{";
    bool first = true;
    for (const auto& [field_name, field_value] : object->fields) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << field_name << "=" << ValueToStringInternal(field_value);
    }
    out << "}";
    return out.str();
}

std::string ArrayToString(const ArrayInstancePtr& array) {
    if (!array) {
        return "null-array";
    }

    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < array->elements.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << ValueToStringInternal(array->elements[i]);
    }
    out << "]";
    return out.str();
}

std::string ValueToStringInternal(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return NumberToString(*number);
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* character = std::get_if<char>(&value)) {
        return std::string(1, *character);
    }
    if (const auto* object = std::get_if<ObjectInstancePtr>(&value)) {
        return ObjectToString(*object);
    }
    if (const auto* array = std::get_if<ArrayInstancePtr>(&value)) {
        return ArrayToString(*array);
    }
    return "null";
}

bool IsNumericLike(const Value& value) {
    return std::holds_alternative<double>(value) || std::holds_alternative<bool>(value) ||
           std::holds_alternative<char>(value);
}

double AsNumber(const Value& value, std::string_view context) {
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? 1.0 : 0.0;
    }
    if (const auto* character = std::get_if<char>(&value)) {
        return static_cast<double>(static_cast<unsigned char>(*character));
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return ParseNumberFromText(*text, context);
    }
    if (std::holds_alternative<ObjectInstancePtr>(value)) {
        throw InterpreterException(std::string("cannot use object as number in ") + std::string(context));
    }
    if (std::holds_alternative<ArrayInstancePtr>(value)) {
        throw InterpreterException(std::string("cannot use array as number in ") + std::string(context));
    }
    throw InterpreterException(std::string("cannot use null value as number in ") + std::string(context));
}

std::int64_t AsInt64(const Value& value, std::string_view context) {
    const double number = AsNumber(value, context);
    if (!std::isfinite(number)) {
        throw InterpreterException("integer in " + std::string(context) + " must be finite");
    }
    const double truncated = std::trunc(number);
    if (truncated != number) {
        throw InterpreterException("integer in " + std::string(context) + " must not have a fractional part");
    }
    return static_cast<std::int64_t>(truncated);
}

bool IsTruthy(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return false;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return !text->empty();
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }
    if (const auto* character = std::get_if<char>(&value)) {
        return *character != '\0';
    }
    if (const auto* object = std::get_if<ObjectInstancePtr>(&value)) {
        return *object != nullptr;
    }
    if (const auto* array = std::get_if<ArrayInstancePtr>(&value)) {
        return *array != nullptr && !(*array)->elements.empty();
    }
    return false;
}

bool ValueEquals(const Value& lhs, const Value& rhs) {
    if (lhs.index() == rhs.index()) {
        if (std::holds_alternative<std::monostate>(lhs)) {
            return true;
        }
        if (const auto* left = std::get_if<double>(&lhs)) {
            return *left == std::get<double>(rhs);
        }
        if (const auto* left = std::get_if<std::string>(&lhs)) {
            return *left == std::get<std::string>(rhs);
        }
        if (const auto* left = std::get_if<bool>(&lhs)) {
            return *left == std::get<bool>(rhs);
        }
        if (const auto* left = std::get_if<char>(&lhs)) {
            return *left == std::get<char>(rhs);
        }
        if (const auto* left = std::get_if<ObjectInstancePtr>(&lhs)) {
            return left->get() == std::get<ObjectInstancePtr>(rhs).get();
        }
        if (const auto* left = std::get_if<ArrayInstancePtr>(&lhs)) {
            return left->get() == std::get<ArrayInstancePtr>(rhs).get();
        }
    }

    if (IsNumericLike(lhs) && IsNumericLike(rhs)) {
        return AsNumber(lhs, "==") == AsNumber(rhs, "==");
    }

    return ValueToStringInternal(lhs) == ValueToStringInternal(rhs);
}

bool ValueLess(const Value& lhs, const Value& rhs) {
    if (IsNumericLike(lhs) && IsNumericLike(rhs)) {
        return AsNumber(lhs, "<") < AsNumber(rhs, "<");
    }
    return ValueToStringInternal(lhs) < ValueToStringInternal(rhs);
}

std::string_view TrimWhitespace(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::size_t AsIndex(const Value& value, std::string_view context) {
    const double raw = AsNumber(value, context);
    if (!std::isfinite(raw)) {
        throw InterpreterException("index in " + std::string(context) + " must be finite");
    }
    if (raw < 0.0) {
        throw InterpreterException("index in " + std::string(context) + " must be non-negative");
    }
    const double truncated = std::floor(raw);
    if (truncated != raw) {
        throw InterpreterException("index in " + std::string(context) + " must be an integer");
    }
    return static_cast<std::size_t>(truncated);
}

std::string JoinCommaSeparated(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << values[i];
    }
    return out.str();
}

std::string DescribeExpr(const gen::Expr& expr) {
    if (const auto* number = dynamic_cast<const gen::Number*>(&expr)) {
        return "num(" + number->value() + ")";
    }
    if (const auto* formatted_string = dynamic_cast<const gen::FormattedString*>(&expr)) {
        return "fstr(" + formatted_string->value() + ")";
    }
    if (const auto* string_literal = dynamic_cast<const gen::StringLiteral*>(&expr)) {
        return "str(" + string_literal->value() + ")";
    }
    if (const auto* char_literal = dynamic_cast<const gen::CharLiteral*>(&expr)) {
        return "char(" + char_literal->value() + ")";
    }
    if (const auto* bool_literal = dynamic_cast<const gen::BoolLiteral*>(&expr)) {
        return "bool(" + bool_literal->value() + ")";
    }
    if (const auto* identifier = dynamic_cast<const gen::Identifier*>(&expr)) {
        return "id(" + identifier->name() + ")";
    }
    if (dynamic_cast<const gen::Add*>(&expr) != nullptr) {
        return "add";
    }
    if (dynamic_cast<const gen::Subtract*>(&expr) != nullptr) {
        return "sub";
    }
    if (dynamic_cast<const gen::Multiply*>(&expr) != nullptr) {
        return "mul";
    }
    if (dynamic_cast<const gen::Divide*>(&expr) != nullptr) {
        return "div";
    }
    if (dynamic_cast<const gen::Modulo*>(&expr) != nullptr) {
        return "mod";
    }
    if (dynamic_cast<const gen::Pow*>(&expr) != nullptr) {
        return "pow";
    }
    if (dynamic_cast<const gen::Negate*>(&expr) != nullptr) {
        return "neg";
    }
    if (dynamic_cast<const gen::LogicalNot*>(&expr) != nullptr) {
        return "not";
    }
    if (dynamic_cast<const gen::BitwiseNot*>(&expr) != nullptr) {
        return "bnot";
    }
    if (dynamic_cast<const gen::LogicalAnd*>(&expr) != nullptr) {
        return "land";
    }
    if (dynamic_cast<const gen::LogicalOr*>(&expr) != nullptr) {
        return "lor";
    }
    if (dynamic_cast<const gen::BitwiseAnd*>(&expr) != nullptr) {
        return "band";
    }
    if (dynamic_cast<const gen::BitwiseOr*>(&expr) != nullptr) {
        return "bor";
    }
    if (dynamic_cast<const gen::BitwiseXor*>(&expr) != nullptr) {
        return "bxor";
    }
    if (dynamic_cast<const gen::ShiftLeft*>(&expr) != nullptr) {
        return "shl";
    }
    if (dynamic_cast<const gen::ShiftRight*>(&expr) != nullptr) {
        return "shr";
    }
    if (dynamic_cast<const gen::Equal*>(&expr) != nullptr) {
        return "eq";
    }
    if (dynamic_cast<const gen::NotEqual*>(&expr) != nullptr) {
        return "neq";
    }
    if (dynamic_cast<const gen::Less*>(&expr) != nullptr) {
        return "lt";
    }
    if (dynamic_cast<const gen::LessEqual*>(&expr) != nullptr) {
        return "lte";
    }
    if (dynamic_cast<const gen::Greater*>(&expr) != nullptr) {
        return "gt";
    }
    if (dynamic_cast<const gen::GreaterEqual*>(&expr) != nullptr) {
        return "gte";
    }
    if (dynamic_cast<const gen::Ternary*>(&expr) != nullptr) {
        return "ternary";
    }
    if (const auto* call = dynamic_cast<const gen::Call*>(&expr)) {
        return "call(" + call->name() + ")";
    }
    if (const auto* array_literal = dynamic_cast<const gen::ArrayLiteral*>(&expr)) {
        return "array[" + std::to_string(array_literal->elements().size()) + "]";
    }
    if (const auto* method_call = dynamic_cast<const gen::MethodCall*>(&expr)) {
        return "call(" + DescribeExpr(method_call->object()) + "." + method_call->name() + ")";
    }
    if (const auto* index = dynamic_cast<const gen::IndexAccess*>(&expr)) {
        return "index(" + DescribeExpr(index->object()) + ")";
    }
    if (const auto* member = dynamic_cast<const gen::MemberAccess*>(&expr)) {
        return "member(" + DescribeExpr(member->object()) + "." + member->member() + ")";
    }
    return "expr";
}

std::string DescribeForInit(const gen::ForInit& init) {
    if (const auto* let_init = dynamic_cast<const gen::ForInitLet*>(&init)) {
        return "let " + let_init->name() + " = " + DescribeExpr(let_init->expr());
    }
    if (const auto* assign_init = dynamic_cast<const gen::ForInitAssign*>(&init)) {
        return "assign " + DescribeExpr(assign_init->target()) + " = " + DescribeExpr(assign_init->expr());
    }
    if (const auto* compound_init = dynamic_cast<const gen::ForInitCompound*>(&init)) {
        return "assign " + DescribeExpr(compound_init->target()) + " " + compound_init->op() + " " +
               DescribeExpr(compound_init->expr());
    }
    if (const auto* expr_init = dynamic_cast<const gen::ForInitExpr*>(&init)) {
        return DescribeExpr(expr_init->expr());
    }
    return "init";
}

std::string DescribeStatement(const gen::Statement& statement) {
    if (const auto* let_stmt = dynamic_cast<const gen::LetStmt*>(&statement)) {
        return "let " + let_stmt->name() + " = " + DescribeExpr(let_stmt->expr());
    }
    if (const auto* assign_stmt = dynamic_cast<const gen::AssignStmt*>(&statement)) {
        return "assign " + DescribeExpr(assign_stmt->target()) + " = " + DescribeExpr(assign_stmt->expr());
    }
    if (const auto* compound_stmt = dynamic_cast<const gen::CompoundAssignStmt*>(&statement)) {
        return "assign " + DescribeExpr(compound_stmt->target()) + " " + compound_stmt->op() + " " +
               DescribeExpr(compound_stmt->expr());
    }
    if (const auto* return_stmt = dynamic_cast<const gen::ReturnStmt*>(&statement)) {
        return "return " + DescribeExpr(return_stmt->expr());
    }
    if (const auto* expr_stmt = dynamic_cast<const gen::ExprStmt*>(&statement)) {
        return DescribeExpr(expr_stmt->expr());
    }
    if (const auto* if_stmt = dynamic_cast<const gen::IfStmt*>(&statement)) {
        std::ostringstream out;
        out << "if (" << DescribeExpr(if_stmt->condition()) << ") then=" << if_stmt->then_body().size()
            << " else=" << if_stmt->else_body().size();
        return out.str();
    }
    if (const auto* while_stmt = dynamic_cast<const gen::WhileStmt*>(&statement)) {
        return "while (" + DescribeExpr(while_stmt->condition()) + ")";
    }
    if (const auto* for_stmt = dynamic_cast<const gen::ForStmt*>(&statement)) {
        return "for (" + DescribeForInit(for_stmt->init()) + "; " + DescribeExpr(for_stmt->condition()) + "; " +
               DescribeForInit(for_stmt->update()) + ")";
    }
    if (const auto* switch_stmt = dynamic_cast<const gen::SwitchStmt*>(&statement)) {
        return "switch (" + DescribeExpr(switch_stmt->condition()) + ") cases=" +
               std::to_string(switch_stmt->cases().size());
    }
    return "stmt";
}

struct LoadedProgramUnit {
    std::string name;
    AST ast;
};

struct RuntimeEnvironment {
    SymbolTable symbols;
    std::unordered_map<std::string, const gen::FunctionDecl*> functions;
    std::unordered_map<std::string, std::unordered_map<std::string, const gen::MethodDecl*>> class_methods;
    std::vector<std::unordered_map<std::string, Value>> scopes;
    std::vector<LoadedProgramUnit> loaded_units;
};

struct ClassMemberSummary {
    std::vector<std::string> fields;
    std::vector<std::string> methods;
    std::unordered_map<std::string, const gen::MethodDecl*> method_nodes;
};

ClassMemberSummary SummarizeClassMembers(const gen::ClassDecl& class_decl) {
    ClassMemberSummary out;
    std::unordered_set<std::string> seen_fields;
    std::unordered_set<std::string> seen_methods;

    for (const std::unique_ptr<gen::ClassMember>& member : class_decl.members()) {
        if (!member) {
            continue;
        }

        if (const auto* field = dynamic_cast<const gen::FieldDecl*>(member.get())) {
            if (!seen_fields.insert(field->name()).second) {
                throw InterpreterException("duplicate field '" + field->name() + "' in class '" + class_decl.name() + "'");
            }
            out.fields.push_back(field->name());
            continue;
        }

        if (const auto* method = dynamic_cast<const gen::MethodDecl*>(member.get())) {
            if (!seen_methods.insert(method->name()).second) {
                throw InterpreterException("duplicate method '" + method->name() + "' in class '" + class_decl.name() + "'");
            }
            out.methods.push_back(method->name());
            out.method_nodes.emplace(method->name(), method);
            continue;
        }

        throw InterpreterException("unsupported class member in class '" + class_decl.name() + "'");
    }

    return out;
}

bool IsBuiltinFunction(std::string_view name) {
    return name == "sin" || name == "cos" || name == "tan" || name == "sqrt" || name == "abs" || name == "exp" ||
           name == "ln" || name == "log10" || name == "pow" || name == "min" || name == "max" || name == "sum" ||
           name == "print" || name == "println" || name == "readln" || name == "input" || name == "read_file" ||
           name == "write_file" || name == "append_file" || name == "file_exists" || name == "file_size" ||
           name == "len" || name == "push" || name == "pop";
}

Value CallBuiltin(std::string_view name, const std::vector<Value>& args) {
    const auto require_count = [&](std::size_t expected) {
        if (args.size() != expected) {
            throw InterpreterException("function '" + std::string(name) + "' expects " + std::to_string(expected) +
                                       " argument(s), got " + std::to_string(args.size()));
        }
    };
    const auto require_min_count = [&](std::size_t minimum) {
        if (args.size() < minimum) {
            throw InterpreterException("function '" + std::string(name) + "' expects at least " +
                                       std::to_string(minimum) + " argument(s), got " + std::to_string(args.size()));
        }
    };

    if (name == "print" || name == "println") {
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) {
                std::cout << ' ';
            }
            std::cout << ValueToStringInternal(args[i]);
        }
        if (name == "println") {
            std::cout << "\n";
        }
        std::cout.flush();
        if (args.empty()) {
            return std::monostate{};
        }
        return args.back();
    }

    if (name == "readln") {
        require_count(0);
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::string{};
        }
        return line;
    }

    if (name == "input") {
        if (args.size() > 1) {
            throw InterpreterException("function 'input' expects 0 or 1 argument(s), got " +
                                       std::to_string(args.size()));
        }
        if (!args.empty()) {
            std::cout << ValueToStringInternal(args[0]);
            std::cout.flush();
        }
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::string{};
        }
        return line;
    }

    if (name == "read_file") {
        require_count(1);
        const std::string path = AsString(args[0]);
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            throw InterpreterException("read_file failed to open: " + path);
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    if (name == "write_file") {
        require_count(2);
        const std::string path = AsString(args[0]);
        const std::string content = AsString(args[1]);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw InterpreterException("write_file failed to open: " + path);
        }
        output << content;
        if (!output.good()) {
            throw InterpreterException("write_file failed to write: " + path);
        }
        return static_cast<double>(content.size());
    }

    if (name == "append_file") {
        require_count(2);
        const std::string path = AsString(args[0]);
        const std::string content = AsString(args[1]);
        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output.is_open()) {
            throw InterpreterException("append_file failed to open: " + path);
        }
        output << content;
        if (!output.good()) {
            throw InterpreterException("append_file failed to write: " + path);
        }
        return static_cast<double>(content.size());
    }

    if (name == "file_exists") {
        require_count(1);
        const std::string path = AsString(args[0]);
        return std::filesystem::exists(path);
    }

    if (name == "file_size") {
        require_count(1);
        const std::string path = AsString(args[0]);
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            throw InterpreterException("file_size failed for '" + path + "': " + error.message());
        }
        return static_cast<double>(size);
    }

    if (name == "len") {
        require_count(1);
        if (const auto* text = std::get_if<std::string>(&args[0])) {
            return static_cast<double>(text->size());
        }
        if (const auto* array = std::get_if<ArrayInstancePtr>(&args[0])) {
            if (!(*array)) {
                throw InterpreterException("len expects a non-null array");
            }
            return static_cast<double>((*array)->elements.size());
        }
        throw InterpreterException("len expects a string or array");
    }

    if (name == "push") {
        require_count(2);
        const auto* array = std::get_if<ArrayInstancePtr>(&args[0]);
        if (array == nullptr || !(*array)) {
            throw InterpreterException("push expects an array as first argument");
        }
        (*array)->elements.push_back(args[1]);
        return static_cast<double>((*array)->elements.size());
    }

    if (name == "pop") {
        require_count(1);
        const auto* array = std::get_if<ArrayInstancePtr>(&args[0]);
        if (array == nullptr || !(*array)) {
            throw InterpreterException("pop expects an array argument");
        }
        if ((*array)->elements.empty()) {
            throw InterpreterException("pop on empty array");
        }
        Value out = (*array)->elements.back();
        (*array)->elements.pop_back();
        return out;
    }

    if (name == "sin") {
        require_count(1);
        return std::sin(AsNumber(args[0], "sin"));
    }
    if (name == "cos") {
        require_count(1);
        return std::cos(AsNumber(args[0], "cos"));
    }
    if (name == "tan") {
        require_count(1);
        return std::tan(AsNumber(args[0], "tan"));
    }
    if (name == "sqrt") {
        require_count(1);
        return std::sqrt(AsNumber(args[0], "sqrt"));
    }
    if (name == "abs") {
        require_count(1);
        return std::fabs(AsNumber(args[0], "abs"));
    }
    if (name == "exp") {
        require_count(1);
        return std::exp(AsNumber(args[0], "exp"));
    }
    if (name == "ln") {
        require_count(1);
        return std::log(AsNumber(args[0], "ln"));
    }
    if (name == "log10") {
        require_count(1);
        return std::log10(AsNumber(args[0], "log10"));
    }
    if (name == "pow") {
        require_count(2);
        return std::pow(AsNumber(args[0], "pow"), AsNumber(args[1], "pow"));
    }
    if (name == "min") {
        require_min_count(1);
        double out = AsNumber(args[0], "min");
        for (std::size_t i = 1; i < args.size(); ++i) {
            const double value = AsNumber(args[i], "min");
            if (value < out) {
                out = value;
            }
        }
        return out;
    }
    if (name == "max") {
        require_min_count(1);
        double out = AsNumber(args[0], "max");
        for (std::size_t i = 1; i < args.size(); ++i) {
            const double value = AsNumber(args[i], "max");
            if (value > out) {
                out = value;
            }
        }
        return out;
    }
    if (name == "sum") {
        require_min_count(1);
        double out = 0.0;
        for (const Value& value : args) {
            out += AsNumber(value, "sum");
        }
        return out;
    }

    throw InterpreterException("unknown function: " + std::string(name));
}

Value LookupVariable(std::string_view name, const RuntimeEnvironment& env) {
    for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
        const auto scope_it = it->find(std::string(name));
        if (scope_it != it->end()) {
            return scope_it->second;
        }
    }

    throw InterpreterException("unknown identifier: " + std::string(name));
}

void AssignVariable(std::string name, Value value, RuntimeEnvironment& env) {
    if (env.scopes.empty()) {
        env.scopes.emplace_back();
    }
    env.scopes.back()[std::move(name)] = std::move(value);
}

bool AssignExistingVariable(std::string_view name, Value value, RuntimeEnvironment& env) {
    for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
        const auto scope_it = it->find(std::string(name));
        if (scope_it != it->end()) {
            scope_it->second = std::move(value);
            return true;
        }
    }
    return false;
}

struct StatementResult {
    bool returned = false;
    Value value = std::monostate{};
};

Value EvaluateExpr(const gen::Expr& expr, RuntimeEnvironment& env);
StatementResult ExecuteStatement(const gen::Statement& statement, RuntimeEnvironment& env);
StatementResult ExecuteStatementList(const std::vector<std::unique_ptr<gen::Statement>>& statements,
                                     RuntimeEnvironment& env, bool create_scope = false);

Value AssignToTarget(const gen::Expr& target, Value value, RuntimeEnvironment& env) {
    if (const auto* identifier = dynamic_cast<const gen::Identifier*>(&target)) {
        if (!AssignExistingVariable(identifier->name(), value, env)) {
            throw InterpreterException("assignment to unknown identifier: " + identifier->name());
        }
        return value;
    }

    if (const auto* member = dynamic_cast<const gen::MemberAccess*>(&target)) {
        const Value object_value = EvaluateExpr(member->object(), env);
        const auto* object = std::get_if<ObjectInstancePtr>(&object_value);
        if (object == nullptr || !(*object)) {
            throw InterpreterException("member assignment requires an object value");
        }
        const auto field_it = (*object)->fields.find(member->member());
        if (field_it == (*object)->fields.end()) {
            throw InterpreterException("unknown field '" + member->member() + "' on type '" + (*object)->class_name + "'");
        }
        field_it->second = value;
        return value;
    }

    if (const auto* index = dynamic_cast<const gen::IndexAccess*>(&target)) {
        const Value object_value = EvaluateExpr(index->object(), env);
        const auto* array = std::get_if<ArrayInstancePtr>(&object_value);
        if (array == nullptr || !(*array)) {
            throw InterpreterException("index assignment requires an array value");
        }
        const std::size_t at = AsIndex(EvaluateExpr(index->index(), env), "index assignment");
        if (at >= (*array)->elements.size()) {
            throw InterpreterException("index assignment out of range: " + std::to_string(at));
        }
        (*array)->elements[at] = value;
        return value;
    }

    throw InterpreterException("invalid assignment target");
}

const gen::MethodDecl* FindMethodOnClass(const RuntimeEnvironment& env, std::string_view class_name,
                                         std::string_view method_name) {
    const auto class_it = env.class_methods.find(std::string(class_name));
    if (class_it == env.class_methods.end()) {
        return nullptr;
    }
    const auto method_it = class_it->second.find(std::string(method_name));
    if (method_it == class_it->second.end()) {
        return nullptr;
    }
    return method_it->second;
}

Value CallUserFunction(const gen::FunctionDecl& function, const std::vector<Value>& args, RuntimeEnvironment& env) {
    if (function.params().size() != args.size()) {
        throw InterpreterException("function '" + function.name() + "' expects " + std::to_string(function.params().size()) +
                                   " argument(s), got " + std::to_string(args.size()));
    }

    std::unordered_map<std::string, Value> frame;
    frame.reserve(function.params().size());
    for (std::size_t i = 0; i < function.params().size(); ++i) {
        frame[function.params()[i]] = args[i];
    }

    env.scopes.push_back(std::move(frame));
    struct FrameGuard {
        RuntimeEnvironment& env_ref;
        ~FrameGuard() {
            env_ref.scopes.pop_back();
        }
    } guard{env};

    const StatementResult result = ExecuteStatementList(function.body(), env, false);
    return result.value;
}

Value CallMethod(const ObjectInstancePtr& self_object, const gen::MethodDecl& method, const std::vector<Value>& args,
                 RuntimeEnvironment& env) {
    if (!self_object) {
        throw InterpreterException("cannot call method '" + method.name() + "' on null object");
    }
    if (method.params().size() != args.size()) {
        throw InterpreterException("method '" + self_object->class_name + "." + method.name() + "' expects " +
                                   std::to_string(method.params().size()) + " argument(s), got " +
                                   std::to_string(args.size()));
    }

    std::unordered_map<std::string, Value> frame;
    frame.reserve(method.params().size() + 2);
    frame["self"] = self_object;
    frame["this"] = self_object;
    for (std::size_t i = 0; i < method.params().size(); ++i) {
        frame[method.params()[i]] = args[i];
    }

    env.scopes.push_back(std::move(frame));
    struct FrameGuard {
        RuntimeEnvironment& env_ref;
        ~FrameGuard() {
            env_ref.scopes.pop_back();
        }
    } guard{env};

    const StatementResult result = ExecuteStatementList(method.body(), env, false);
    return result.value;
}

Value CallMethodByName(const ObjectInstancePtr& self_object, std::string_view method_name, const std::vector<Value>& args,
                       RuntimeEnvironment& env) {
    if (!self_object) {
        throw InterpreterException("cannot call method '" + std::string(method_name) + "' on null object");
    }
    const gen::MethodDecl* method = FindMethodOnClass(env, self_object->class_name, method_name);
    if (method == nullptr) {
        throw InterpreterException("unknown method '" + std::string(method_name) + "' on type '" + self_object->class_name +
                                   "'");
    }
    return CallMethod(self_object, *method, args, env);
}

bool TryCallOperator(const Value& target, std::string_view method_name, const std::vector<Value>& args,
                     RuntimeEnvironment& env, Value& out) {
    const auto* object = std::get_if<ObjectInstancePtr>(&target);
    if (object == nullptr || !(*object)) {
        return false;
    }
    const gen::MethodDecl* method = FindMethodOnClass(env, (*object)->class_name, method_name);
    if (method == nullptr) {
        return false;
    }
    out = CallMethod(*object, *method, args, env);
    return true;
}

bool TryCallOperatorBool(const Value& target, std::string_view method_name, const std::vector<Value>& args,
                         RuntimeEnvironment& env, bool& out) {
    Value method_result;
    if (!TryCallOperator(target, method_name, args, env, method_result)) {
        return false;
    }
    out = IsTruthy(method_result);
    return true;
}

Value ApplyCompoundOperation(std::string_view op, const Value& lhs, const Value& rhs, RuntimeEnvironment& env) {
    Value overloaded_result;
    if (op == "+=") {
        if (TryCallOperator(lhs, "__add__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__radd__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs)) {
            return ValueToStringInternal(lhs) + ValueToStringInternal(rhs);
        }
        return AsNumber(lhs, "+=") + AsNumber(rhs, "+=");
    }
    if (op == "-=") {
        if (TryCallOperator(lhs, "__sub__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rsub__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "-=") - AsNumber(rhs, "-=");
    }
    if (op == "*=") {
        if (TryCallOperator(lhs, "__mul__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmul__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "*=") * AsNumber(rhs, "*=");
    }
    if (op == "/=") {
        if (TryCallOperator(lhs, "__div__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rdiv__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "/=");
        if (rhs_number == 0.0) {
            throw InterpreterException("division by zero");
        }
        return AsNumber(lhs, "/=") / rhs_number;
    }
    if (op == "%=") {
        if (TryCallOperator(lhs, "__mod__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmod__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "%=");
        if (rhs_number == 0.0) {
            throw InterpreterException("modulo by zero");
        }
        return std::fmod(AsNumber(lhs, "%="), rhs_number);
    }
    if (op == "&=") {
        return static_cast<double>(AsInt64(lhs, "&=") & AsInt64(rhs, "&="));
    }
    if (op == "|=") {
        return static_cast<double>(AsInt64(lhs, "|=") | AsInt64(rhs, "|="));
    }
    if (op == "^=") {
        return static_cast<double>(AsInt64(lhs, "^=") ^ AsInt64(rhs, "^="));
    }
    if (op == "<<=") {
        const std::int64_t shift = AsInt64(rhs, "<<=");
        if (shift < 0 || shift > 63) {
            throw InterpreterException("shift count out of range in <<=");
        }
        return static_cast<double>(AsInt64(lhs, "<<=") << shift);
    }
    if (op == ">>=") {
        const std::int64_t shift = AsInt64(rhs, ">>=");
        if (shift < 0 || shift > 63) {
            throw InterpreterException("shift count out of range in >>=");
        }
        return static_cast<double>(AsInt64(lhs, ">>=") >> shift);
    }

    throw InterpreterException("unsupported compound assignment operator: " + std::string(op));
}

Value EvaluateEmbeddedExpression(std::string_view expression_text, RuntimeEnvironment& env) {
    const std::string trimmed = std::string(TrimWhitespace(expression_text));
    if (trimmed.empty()) {
        throw InterpreterException("empty formatted-string expression");
    }

    const AST embedded_ast = ParseProgram(trimmed + ";");
    const gen::Program& program = RequireProgram(embedded_ast);
    const auto& items = program.items().items();
    if (items.size() != 1 || items.front() == nullptr) {
        throw InterpreterException("formatted-string expression must parse as one expression statement: " + trimmed);
    }

    const auto* top_statement = dynamic_cast<const gen::TopStatement*>(items.front().get());
    if (top_statement == nullptr) {
        throw InterpreterException("formatted-string expression did not parse as a statement: " + trimmed);
    }

    const auto* expr_statement = dynamic_cast<const gen::ExprStmt*>(&top_statement->statement());
    if (expr_statement == nullptr) {
        throw InterpreterException("formatted-string expression must be an expression: " + trimmed);
    }

    return EvaluateExpr(expr_statement->expr(), env);
}

std::string EvaluateFormattedStringLiteral(std::string_view literal, RuntimeEnvironment& env) {
    if (literal.size() < 3 || literal[0] != 'f' || literal[1] != '"' || literal.back() != '"') {
        throw InterpreterException("invalid formatted string literal: " + std::string(literal));
    }

    const std::string_view body = literal.substr(2, literal.size() - 3);
    std::string out;
    out.reserve(body.size());

    std::size_t i = 0;
    while (i < body.size()) {
        const char c = body[i];
        if (c == '\\') {
            if (i + 1 >= body.size()) {
                throw InterpreterException("dangling escape in formatted string");
            }
            const char next = body[++i];
            switch (next) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case '0':
                    out.push_back('\0');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '"':
                    out.push_back('"');
                    break;
                case '\'':
                    out.push_back('\'');
                    break;
                default:
                    out.push_back(next);
                    break;
            }
            ++i;
            continue;
        }

        if (c == '{') {
            if (i + 1 < body.size() && body[i + 1] == '{') {
                out.push_back('{');
                i += 2;
                continue;
            }

            std::size_t close = i + 1;
            int depth = 1;
            bool in_single_quote = false;
            bool in_double_quote = false;
            bool escaped = false;
            for (; close < body.size(); ++close) {
                const char ch = body[close];
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (ch == '\\') {
                    escaped = true;
                    continue;
                }
                if (in_single_quote) {
                    if (ch == '\'') {
                        in_single_quote = false;
                    }
                    continue;
                }
                if (in_double_quote) {
                    if (ch == '"') {
                        in_double_quote = false;
                    }
                    continue;
                }
                if (ch == '\'') {
                    in_single_quote = true;
                    continue;
                }
                if (ch == '"') {
                    in_double_quote = true;
                    continue;
                }
                if (ch == '{') {
                    ++depth;
                    continue;
                }
                if (ch == '}') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
            }
            if (close >= body.size()) {
                throw InterpreterException("unmatched '{' in formatted string");
            }

            const std::string_view expression_text = TrimWhitespace(body.substr(i + 1, close - (i + 1)));
            if (expression_text.empty()) {
                throw InterpreterException("empty {} expression in formatted string");
            }

            out += ValueToStringInternal(EvaluateEmbeddedExpression(expression_text, env));
            i = close + 1;
            continue;
        }

        if (c == '}') {
            if (i + 1 < body.size() && body[i + 1] == '}') {
                out.push_back('}');
                i += 2;
                continue;
            }
            throw InterpreterException("unmatched '}' in formatted string");
        }

        out.push_back(c);
        ++i;
    }

    return out;
}

std::vector<Value> EvaluateArgs(const std::vector<std::unique_ptr<gen::Expr>>& args, RuntimeEnvironment& env) {
    std::vector<Value> out;
    out.reserve(args.size());
    for (const std::unique_ptr<gen::Expr>& arg : args) {
        if (!arg) {
            throw InterpreterException("null call argument node");
        }
        out.push_back(EvaluateExpr(*arg, env));
    }
    return out;
}

StatementResult ExecuteStatementList(const std::vector<std::unique_ptr<gen::Statement>>& statements,
                                     RuntimeEnvironment& env, bool create_scope) {
    if (create_scope) {
        env.scopes.emplace_back();
    }
    struct ScopeGuard {
        RuntimeEnvironment& env_ref;
        bool active = false;
        ~ScopeGuard() {
            if (active) {
                env_ref.scopes.pop_back();
            }
        }
    } guard{env, create_scope};

    StatementResult last;
    for (const std::unique_ptr<gen::Statement>& statement : statements) {
        if (statement == nullptr) {
            continue;
        }
        last = ExecuteStatement(*statement, env);
        if (last.returned) {
            return last;
        }
    }
    return last;
}

void ExecuteForInit(const gen::ForInit& init, RuntimeEnvironment& env) {
    if (const auto* let_init = dynamic_cast<const gen::ForInitLet*>(&init)) {
        AssignVariable(let_init->name(), EvaluateExpr(let_init->expr(), env), env);
        return;
    }
    if (const auto* assign_init = dynamic_cast<const gen::ForInitAssign*>(&init)) {
        (void) AssignToTarget(assign_init->target(), EvaluateExpr(assign_init->expr(), env), env);
        return;
    }
    if (const auto* compound_init = dynamic_cast<const gen::ForInitCompound*>(&init)) {
        const Value current = EvaluateExpr(compound_init->target(), env);
        const Value rhs = EvaluateExpr(compound_init->expr(), env);
        (void) AssignToTarget(compound_init->target(), ApplyCompoundOperation(compound_init->op(), current, rhs, env), env);
        return;
    }
    if (const auto* expr_init = dynamic_cast<const gen::ForInitExpr*>(&init)) {
        (void) EvaluateExpr(expr_init->expr(), env);
        return;
    }
    throw InterpreterException("unsupported for-init node");
}

Value EvaluateExpr(const gen::Expr& expr, RuntimeEnvironment& env) {
    if (const auto* number = dynamic_cast<const gen::Number*>(&expr)) {
        return ParseNumberFromText(number->value(), "number literal");
    }
    if (const auto* formatted_string = dynamic_cast<const gen::FormattedString*>(&expr)) {
        return EvaluateFormattedStringLiteral(formatted_string->value(), env);
    }
    if (const auto* string_literal = dynamic_cast<const gen::StringLiteral*>(&expr)) {
        return ParseQuotedLiteral(string_literal->value(), '"', "string");
    }
    if (const auto* char_literal = dynamic_cast<const gen::CharLiteral*>(&expr)) {
        const std::string decoded = ParseQuotedLiteral(char_literal->value(), '\'', "char");
        if (decoded.size() != 1) {
            throw InterpreterException("char literal must decode to exactly one character");
        }
        return decoded[0];
    }
    if (const auto* bool_literal = dynamic_cast<const gen::BoolLiteral*>(&expr)) {
        if (bool_literal->value() == "true") {
            return true;
        }
        if (bool_literal->value() == "false") {
            return false;
        }
        throw InterpreterException("invalid bool literal: " + bool_literal->value());
    }
    if (const auto* identifier = dynamic_cast<const gen::Identifier*>(&expr)) {
        return LookupVariable(identifier->name(), env);
    }
    if (const auto* array_literal = dynamic_cast<const gen::ArrayLiteral*>(&expr)) {
        auto array = std::make_shared<ArrayInstance>();
        array->elements.reserve(array_literal->elements().size());
        for (const std::unique_ptr<gen::Expr>& element : array_literal->elements()) {
            if (!element) {
                throw InterpreterException("null element in array literal");
            }
            array->elements.push_back(EvaluateExpr(*element, env));
        }
        return array;
    }
    if (const auto* index = dynamic_cast<const gen::IndexAccess*>(&expr)) {
        const Value object_value = EvaluateExpr(index->object(), env);
        const std::size_t at = AsIndex(EvaluateExpr(index->index(), env), "index access");

        if (const auto* array = std::get_if<ArrayInstancePtr>(&object_value)) {
            if (!(*array)) {
                throw InterpreterException("index access on null array");
            }
            if (at >= (*array)->elements.size()) {
                throw InterpreterException("index access out of range: " + std::to_string(at));
            }
            return (*array)->elements[at];
        }
        if (const auto* text = std::get_if<std::string>(&object_value)) {
            if (at >= text->size()) {
                throw InterpreterException("string index out of range: " + std::to_string(at));
            }
            return static_cast<char>((*text)[at]);
        }

        throw InterpreterException("index access requires an array or string value");
    }
    if (const auto* logical_or = dynamic_cast<const gen::LogicalOr*>(&expr)) {
        if (IsTruthy(EvaluateExpr(logical_or->lhs(), env))) {
            return true;
        }
        return IsTruthy(EvaluateExpr(logical_or->rhs(), env));
    }
    if (const auto* logical_and = dynamic_cast<const gen::LogicalAnd*>(&expr)) {
        if (!IsTruthy(EvaluateExpr(logical_and->lhs(), env))) {
            return false;
        }
        return IsTruthy(EvaluateExpr(logical_and->rhs(), env));
    }
    if (const auto* add = dynamic_cast<const gen::Add*>(&expr)) {
        const Value lhs = EvaluateExpr(add->lhs(), env);
        const Value rhs = EvaluateExpr(add->rhs(), env);
        Value overloaded_result;
        if (TryCallOperator(lhs, "__add__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__radd__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs)) {
            return ValueToStringInternal(lhs) + ValueToStringInternal(rhs);
        }
        return AsNumber(lhs, "+") + AsNumber(rhs, "+");
    }
    if (const auto* sub = dynamic_cast<const gen::Subtract*>(&expr)) {
        const Value lhs = EvaluateExpr(sub->lhs(), env);
        const Value rhs = EvaluateExpr(sub->rhs(), env);
        Value overloaded_result;
        if (TryCallOperator(lhs, "__sub__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rsub__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "-") - AsNumber(rhs, "-");
    }
    if (const auto* mul = dynamic_cast<const gen::Multiply*>(&expr)) {
        const Value lhs = EvaluateExpr(mul->lhs(), env);
        const Value rhs = EvaluateExpr(mul->rhs(), env);
        Value overloaded_result;
        if (TryCallOperator(lhs, "__mul__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmul__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return AsNumber(lhs, "*") * AsNumber(rhs, "*");
    }
    if (const auto* div = dynamic_cast<const gen::Divide*>(&expr)) {
        const Value lhs = EvaluateExpr(div->lhs(), env);
        const Value rhs_value = EvaluateExpr(div->rhs(), env);
        Value overloaded_result;
        if (TryCallOperator(lhs, "__div__", {rhs_value}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs_value, "__rdiv__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs = AsNumber(rhs_value, "/");
        if (rhs == 0.0) {
            throw InterpreterException("division by zero");
        }
        return AsNumber(lhs, "/") / rhs;
    }
    if (const auto* mod = dynamic_cast<const gen::Modulo*>(&expr)) {
        const Value lhs = EvaluateExpr(mod->lhs(), env);
        const Value rhs = EvaluateExpr(mod->rhs(), env);
        Value overloaded_result;
        if (TryCallOperator(lhs, "__mod__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rmod__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        const double rhs_number = AsNumber(rhs, "%");
        if (rhs_number == 0.0) {
            throw InterpreterException("modulo by zero");
        }
        return std::fmod(AsNumber(lhs, "%"), rhs_number);
    }
    if (const auto* pow_expr = dynamic_cast<const gen::Pow*>(&expr)) {
        const Value lhs = EvaluateExpr(pow_expr->lhs(), env);
        const Value rhs = EvaluateExpr(pow_expr->rhs(), env);
        Value overloaded_result;
        if (TryCallOperator(lhs, "__pow__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperator(rhs, "__rpow__", {lhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return std::pow(AsNumber(lhs, "**"), AsNumber(rhs, "**"));
    }
    if (const auto* neg = dynamic_cast<const gen::Negate*>(&expr)) {
        const Value operand = EvaluateExpr(neg->expr(), env);
        Value overloaded_result;
        if (TryCallOperator(operand, "__neg__", {}, env, overloaded_result)) {
            return overloaded_result;
        }
        return -AsNumber(operand, "unary -");
    }
    if (const auto* logical_not = dynamic_cast<const gen::LogicalNot*>(&expr)) {
        return !IsTruthy(EvaluateExpr(logical_not->expr(), env));
    }
    if (const auto* bitwise_not = dynamic_cast<const gen::BitwiseNot*>(&expr)) {
        return static_cast<double>(~AsInt64(EvaluateExpr(bitwise_not->expr(), env), "~"));
    }
    if (const auto* bitwise_and = dynamic_cast<const gen::BitwiseAnd*>(&expr)) {
        return static_cast<double>(AsInt64(EvaluateExpr(bitwise_and->lhs(), env), "&") &
                                   AsInt64(EvaluateExpr(bitwise_and->rhs(), env), "&"));
    }
    if (const auto* bitwise_or = dynamic_cast<const gen::BitwiseOr*>(&expr)) {
        return static_cast<double>(AsInt64(EvaluateExpr(bitwise_or->lhs(), env), "|") |
                                   AsInt64(EvaluateExpr(bitwise_or->rhs(), env), "|"));
    }
    if (const auto* bitwise_xor = dynamic_cast<const gen::BitwiseXor*>(&expr)) {
        return static_cast<double>(AsInt64(EvaluateExpr(bitwise_xor->lhs(), env), "^") ^
                                   AsInt64(EvaluateExpr(bitwise_xor->rhs(), env), "^"));
    }
    if (const auto* shift_left = dynamic_cast<const gen::ShiftLeft*>(&expr)) {
        const std::int64_t shift = AsInt64(EvaluateExpr(shift_left->rhs(), env), "<<");
        if (shift < 0 || shift > 63) {
            throw InterpreterException("shift count out of range in <<");
        }
        return static_cast<double>(AsInt64(EvaluateExpr(shift_left->lhs(), env), "<<") << shift);
    }
    if (const auto* shift_right = dynamic_cast<const gen::ShiftRight*>(&expr)) {
        const std::int64_t shift = AsInt64(EvaluateExpr(shift_right->rhs(), env), ">>");
        if (shift < 0 || shift > 63) {
            throw InterpreterException("shift count out of range in >>");
        }
        return static_cast<double>(AsInt64(EvaluateExpr(shift_right->lhs(), env), ">>") >> shift);
    }
    if (const auto* eq = dynamic_cast<const gen::Equal*>(&expr)) {
        const Value lhs = EvaluateExpr(eq->lhs(), env);
        const Value rhs = EvaluateExpr(eq->rhs(), env);
        bool overloaded_result = false;
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return ValueEquals(lhs, rhs);
    }
    if (const auto* neq = dynamic_cast<const gen::NotEqual*>(&expr)) {
        const Value lhs = EvaluateExpr(neq->lhs(), env);
        const Value rhs = EvaluateExpr(neq->rhs(), env);
        bool overloaded_result = false;
        if (TryCallOperatorBool(lhs, "__ne__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, overloaded_result)) {
            return !overloaded_result;
        }
        return !ValueEquals(lhs, rhs);
    }
    if (const auto* lt = dynamic_cast<const gen::Less*>(&expr)) {
        const Value lhs = EvaluateExpr(lt->lhs(), env);
        const Value rhs = EvaluateExpr(lt->rhs(), env);
        bool overloaded_result = false;
        if (TryCallOperatorBool(lhs, "__lt__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return ValueLess(lhs, rhs);
    }
    if (const auto* lte = dynamic_cast<const gen::LessEqual*>(&expr)) {
        const Value lhs = EvaluateExpr(lte->lhs(), env);
        const Value rhs = EvaluateExpr(lte->rhs(), env);
        bool overloaded_result = false;
        if (TryCallOperatorBool(lhs, "__le__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        bool lt_result = false;
        if (TryCallOperatorBool(lhs, "__lt__", {rhs}, env, lt_result) && lt_result) {
            return true;
        }
        bool eq_result = false;
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, eq_result)) {
            return eq_result;
        }
        return ValueLess(lhs, rhs) || ValueEquals(lhs, rhs);
    }
    if (const auto* gt = dynamic_cast<const gen::Greater*>(&expr)) {
        const Value lhs = EvaluateExpr(gt->lhs(), env);
        const Value rhs = EvaluateExpr(gt->rhs(), env);
        bool overloaded_result = false;
        if (TryCallOperatorBool(lhs, "__gt__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        return ValueLess(rhs, lhs);
    }
    if (const auto* gte = dynamic_cast<const gen::GreaterEqual*>(&expr)) {
        const Value lhs = EvaluateExpr(gte->lhs(), env);
        const Value rhs = EvaluateExpr(gte->rhs(), env);
        bool overloaded_result = false;
        if (TryCallOperatorBool(lhs, "__ge__", {rhs}, env, overloaded_result)) {
            return overloaded_result;
        }
        bool gt_result = false;
        if (TryCallOperatorBool(lhs, "__gt__", {rhs}, env, gt_result) && gt_result) {
            return true;
        }
        bool eq_result = false;
        if (TryCallOperatorBool(lhs, "__eq__", {rhs}, env, eq_result)) {
            return eq_result;
        }
        return ValueLess(rhs, lhs) || ValueEquals(lhs, rhs);
    }
    if (const auto* ternary = dynamic_cast<const gen::Ternary*>(&expr)) {
        if (IsTruthy(EvaluateExpr(ternary->condition(), env))) {
            return EvaluateExpr(ternary->when_true(), env);
        }
        return EvaluateExpr(ternary->when_false(), env);
    }
    if (const auto* method_call = dynamic_cast<const gen::MethodCall*>(&expr)) {
        const Value object_value = EvaluateExpr(method_call->object(), env);
        const auto* object = std::get_if<ObjectInstancePtr>(&object_value);
        if (object == nullptr || !(*object)) {
            throw InterpreterException("method call requires an object value");
        }
        const std::vector<Value> args = EvaluateArgs(method_call->args(), env);
        return CallMethodByName(*object, method_call->name(), args, env);
    }
    if (const auto* member = dynamic_cast<const gen::MemberAccess*>(&expr)) {
        const Value object_value = EvaluateExpr(member->object(), env);
        const auto* object = std::get_if<ObjectInstancePtr>(&object_value);
        if (object == nullptr || !(*object)) {
            throw InterpreterException("member access requires an object value");
        }
        const auto field_it = (*object)->fields.find(member->member());
        if (field_it == (*object)->fields.end()) {
            throw InterpreterException("unknown field '" + member->member() + "' on type '" + (*object)->class_name + "'");
        }
        return field_it->second;
    }
    if (const auto* call = dynamic_cast<const gen::Call*>(&expr)) {
        const std::vector<Value> args = EvaluateArgs(call->args(), env);

        const auto user_it = env.functions.find(call->name());
        if (user_it != env.functions.end()) {
            return CallUserFunction(*user_it->second, args, env);
        }

        if (const SymbolTable::ClassInfo* class_info = env.symbols.FindClass(call->name())) {
            if (class_info->field_names.size() != args.size()) {
                throw InterpreterException("type '" + class_info->name + "' expects " +
                                           std::to_string(class_info->field_names.size()) + " constructor arg(s), got " +
                                           std::to_string(args.size()));
            }

            auto object = std::make_shared<ObjectInstance>();
            object->class_name = class_info->name;
            for (std::size_t i = 0; i < args.size(); ++i) {
                object->fields[class_info->field_names[i]] = args[i];
            }
            return object;
        }

        if (IsBuiltinFunction(call->name())) {
            return CallBuiltin(call->name(), args);
        }

        throw InterpreterException("unknown function or type: " + call->name());
    }

    throw InterpreterException("unsupported expression node");
}

StatementResult ExecuteStatement(const gen::Statement& statement, RuntimeEnvironment& env) {
    if (const auto* let_stmt = dynamic_cast<const gen::LetStmt*>(&statement)) {
        Value value = EvaluateExpr(let_stmt->expr(), env);
        AssignVariable(let_stmt->name(), value, env);
        return StatementResult{.returned = false, .value = std::move(value)};
    }
    if (const auto* assign_stmt = dynamic_cast<const gen::AssignStmt*>(&statement)) {
        Value value = EvaluateExpr(assign_stmt->expr(), env);
        return StatementResult{
            .returned = false,
            .value = AssignToTarget(assign_stmt->target(), std::move(value), env),
        };
    }
    if (const auto* compound_stmt = dynamic_cast<const gen::CompoundAssignStmt*>(&statement)) {
        const Value current = EvaluateExpr(compound_stmt->target(), env);
        const Value rhs = EvaluateExpr(compound_stmt->expr(), env);
        return StatementResult{
            .returned = false,
            .value = AssignToTarget(compound_stmt->target(),
                                    ApplyCompoundOperation(compound_stmt->op(), current, rhs, env), env),
        };
    }
    if (const auto* return_stmt = dynamic_cast<const gen::ReturnStmt*>(&statement)) {
        return StatementResult{.returned = true, .value = EvaluateExpr(return_stmt->expr(), env)};
    }
    if (const auto* expr_stmt = dynamic_cast<const gen::ExprStmt*>(&statement)) {
        return StatementResult{.returned = false, .value = EvaluateExpr(expr_stmt->expr(), env)};
    }
    if (const auto* if_stmt = dynamic_cast<const gen::IfStmt*>(&statement)) {
        if (IsTruthy(EvaluateExpr(if_stmt->condition(), env))) {
            return ExecuteStatementList(if_stmt->then_body(), env, true);
        }
        return ExecuteStatementList(if_stmt->else_body(), env, true);
    }
    if (const auto* while_stmt = dynamic_cast<const gen::WhileStmt*>(&statement)) {
        Value last = std::monostate{};
        while (IsTruthy(EvaluateExpr(while_stmt->condition(), env))) {
            const StatementResult body = ExecuteStatementList(while_stmt->body(), env, true);
            if (body.returned) {
                return body;
            }
            last = body.value;
        }
        return StatementResult{.returned = false, .value = std::move(last)};
    }
    if (const auto* for_stmt = dynamic_cast<const gen::ForStmt*>(&statement)) {
        env.scopes.emplace_back();
        struct ScopeGuard {
            RuntimeEnvironment& env_ref;
            ~ScopeGuard() {
                env_ref.scopes.pop_back();
            }
        } guard{env};

        ExecuteForInit(for_stmt->init(), env);

        Value last = std::monostate{};
        while (IsTruthy(EvaluateExpr(for_stmt->condition(), env))) {
            const StatementResult body = ExecuteStatementList(for_stmt->body(), env, true);
            if (body.returned) {
                return body;
            }
            last = body.value;
            ExecuteForInit(for_stmt->update(), env);
        }
        return StatementResult{.returned = false, .value = std::move(last)};
    }
    if (const auto* switch_stmt = dynamic_cast<const gen::SwitchStmt*>(&statement)) {
        const Value condition = EvaluateExpr(switch_stmt->condition(), env);
        for (const std::unique_ptr<gen::SwitchCase>& switch_case : switch_stmt->cases()) {
            if (!switch_case) {
                continue;
            }
            if (ValueEquals(condition, EvaluateExpr(switch_case->match(), env))) {
                return ExecuteStatementList(switch_case->body(), env, true);
            }
        }
        return ExecuteStatementList(switch_stmt->default_body(), env, true);
    }

    throw InterpreterException("unsupported statement node");
}

void CollectTopLevelGlobalFromStatement(const gen::Statement& statement, SymbolTable& symbols) {
    if (const auto* let_stmt = dynamic_cast<const gen::LetStmt*>(&statement)) {
        (void) symbols.DeclareGlobal(let_stmt->name());
        return;
    }
}

} // namespace

std::string ValueToString(const Value& value) {
    return ValueToStringInternal(value);
}

AST ParseProgram(std::string_view source_text) {
    try {
        GeneratedLexer lexer(source_text);
        const std::vector<GeneratedToken> tokens = lexer.Tokenize();
        const std::vector<compiler::parsergen::GenericToken> parser_tokens = ToGenericTokens(tokens);

        GeneratedParser parser;
        return parser.ParseToAST(parser_tokens);
    } catch (const InterpreterException&) {
        throw;
    } catch (const compiler::parsergen::ParseException& ex) {
        throw InterpreterException("internal grammar parse error at " + std::to_string(ex.line()) + ":" +
                                   std::to_string(ex.column()) + ": " + ex.what());
    } catch (const compiler::parsergen::BuildException& ex) {
        throw InterpreterException(std::string("internal parser generator build error: ") + ex.what());
    } catch (const std::exception& ex) {
        throw InterpreterException(ex.what());
    }
}

ProgramAnnotation AnnotateProgram(const AST& ast) {
    const gen::Program& program = RequireProgram(ast);

    ProgramAnnotation out;
    for (const std::unique_ptr<gen::Item>& item : program.items().items()) {
        if (!item) {
            continue;
        }

        if (const auto* function = dynamic_cast<const gen::FunctionDecl*>(item.get())) {
            out.flattened_items.push_back("fn " + function->name() + "(" + JoinCommaSeparated(function->params()) + ")");
            if (!out.symbols.DeclareFunction(function->name())) {
                throw InterpreterException("duplicate function declaration: " + function->name());
            }
            out.function_parameters[function->name()] = function->params();

            std::vector<std::string> statements;
            statements.reserve(function->body().size());
            for (const std::unique_ptr<gen::Statement>& stmt : function->body()) {
                if (stmt) {
                    statements.push_back(DescribeStatement(*stmt));
                }
            }
            out.function_statements[function->name()] = std::move(statements);
            continue;
        }

        if (const auto* class_decl = dynamic_cast<const gen::ClassDecl*>(item.get())) {
            out.flattened_items.push_back("class " + class_decl->name());
            const ClassMemberSummary summary = SummarizeClassMembers(*class_decl);
            if (!out.symbols.DeclareClass(class_decl->name(), summary.fields, summary.methods)) {
                throw InterpreterException("duplicate class declaration: " + class_decl->name());
            }
            out.class_fields[class_decl->name()] = summary.fields;
            out.class_methods[class_decl->name()] = summary.methods;
            continue;
        }

        if (const auto* top_stmt = dynamic_cast<const gen::TopStatement*>(item.get())) {
            CollectTopLevelGlobalFromStatement(top_stmt->statement(), out.symbols);
            out.flattened_items.push_back(DescribeStatement(top_stmt->statement()));
            continue;
        }
    }

    return out;
}

namespace {

void RegisterProgramDeclarations(const gen::Program& program, RuntimeEnvironment& env, std::string_view unit_name) {
    for (const std::unique_ptr<gen::Item>& item : program.items().items()) {
        if (!item) {
            continue;
        }

        if (const auto* function = dynamic_cast<const gen::FunctionDecl*>(item.get())) {
            if (!env.symbols.DeclareFunction(function->name())) {
                throw InterpreterException("duplicate function declaration: " + function->name() + " (" + std::string(unit_name) +
                                           ")");
            }
            if (!env.functions.emplace(function->name(), function).second) {
                throw InterpreterException("duplicate function declaration: " + function->name() + " (" + std::string(unit_name) +
                                           ")");
            }
            continue;
        }

        if (const auto* class_decl = dynamic_cast<const gen::ClassDecl*>(item.get())) {
            const ClassMemberSummary summary = SummarizeClassMembers(*class_decl);
            if (!env.symbols.DeclareClass(class_decl->name(), summary.fields, summary.methods)) {
                throw InterpreterException("duplicate class declaration: " + class_decl->name() + " (" +
                                           std::string(unit_name) + ")");
            }

            auto& method_map = env.class_methods[class_decl->name()];
            for (const auto& [method_name, method_node] : summary.method_nodes) {
                if (!method_map.emplace(method_name, method_node).second) {
                    throw InterpreterException("duplicate method declaration: " + class_decl->name() + "." + method_name +
                                               " (" + std::string(unit_name) + ")");
                }
            }
        }
    }
}

Value ExecuteTopLevelStatements(const gen::Program& program, RuntimeEnvironment& env, std::string_view unit_name,
                                bool allow_top_level_return) {
    Value last_value = std::monostate{};
    for (const std::unique_ptr<gen::Item>& item : program.items().items()) {
        if (const auto* top_stmt = dynamic_cast<const gen::TopStatement*>(item.get())) {
            const StatementResult result = ExecuteStatement(top_stmt->statement(), env);
            last_value = result.value;
            if (result.returned) {
                if (allow_top_level_return) {
                    return last_value;
                }
                throw InterpreterException("top-level return is not allowed in " + std::string(unit_name));
            }
        }
    }
    return last_value;
}

std::vector<std::filesystem::path> ListStandardLibraryFiles() {
    std::vector<std::filesystem::path> out;
#ifdef INTERPRETER_STDLIB_DIR
    const std::filesystem::path stdlib_dir(INTERPRETER_STDLIB_DIR);
    if (!std::filesystem::exists(stdlib_dir)) {
        throw InterpreterException("standard library directory does not exist: " + stdlib_dir.string());
    }
    if (!std::filesystem::is_directory(stdlib_dir)) {
        throw InterpreterException("standard library path is not a directory: " + stdlib_dir.string());
    }

    const std::vector<std::string> ordered_files = {
        "Core.txt",
        "IO.txt",
        "Math.txt",
        "Complex.txt",
    };

    std::unordered_set<std::string> known_names;
    known_names.reserve(ordered_files.size());
    for (const std::string& file_name : ordered_files) {
        const std::filesystem::path component_path = stdlib_dir / file_name;
        if (!std::filesystem::exists(component_path) || !std::filesystem::is_regular_file(component_path)) {
            throw InterpreterException("required standard library component is missing: " + component_path.string());
        }
        out.push_back(component_path);
        known_names.insert(file_name);
    }

    std::vector<std::filesystem::path> extra_files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(stdlib_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string file_name = entry.path().filename().string();
        if (known_names.contains(file_name)) {
            continue;
        }
        extra_files.push_back(entry.path());
    }
    std::sort(extra_files.begin(), extra_files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        return lhs.filename().string() < rhs.filename().string();
    });
    out.insert(out.end(), extra_files.begin(), extra_files.end());
#endif
    return out;
}

AST ParseProgramFile(const std::filesystem::path& source_path) {
    try {
        return ParseProgram(compiler::common::ReadTextFile(source_path));
    } catch (const std::exception& ex) {
        throw InterpreterException("failed to load standard library component '" + source_path.string() + "': " + ex.what());
    }
}

void LoadStandardLibrary(RuntimeEnvironment& env) {
    const std::vector<std::filesystem::path> components = ListStandardLibraryFiles();
    env.loaded_units.clear();
    env.loaded_units.reserve(components.size());
    for (const std::filesystem::path& component_path : components) {
        env.loaded_units.push_back(LoadedProgramUnit{
            .name = component_path.string(),
            .ast = ParseProgramFile(component_path),
        });
    }

    for (const LoadedProgramUnit& unit : env.loaded_units) {
        RegisterProgramDeclarations(RequireProgram(unit.ast), env, unit.name);
    }
    for (const LoadedProgramUnit& unit : env.loaded_units) {
        (void) ExecuteTopLevelStatements(RequireProgram(unit.ast), env, unit.name, false);
    }
}

} // namespace

Value InterpretProgram(const AST& ast) {
    const gen::Program& program = RequireProgram(ast);

    RuntimeEnvironment env;
    env.scopes.emplace_back();
    env.scopes.front()["pi"] = std::acos(-1.0);
    env.scopes.front()["e"] = std::exp(1.0);
    env.scopes.front()["tau"] = 2.0 * std::acos(-1.0);

    LoadStandardLibrary(env);
    RegisterProgramDeclarations(program, env, "<program>");
    return ExecuteTopLevelStatements(program, env, "<program>", true);
}

Value InterpretSource(std::string_view source_text) {
    const AST ast = ParseProgram(source_text);
    return InterpretProgram(ast);
}

} // namespace compiler::interpreter
