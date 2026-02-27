#include "Interpreter.h"

#include "GeneratedParser.h"
#include "LanguageLexer.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
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
        case GeneratedTokenKind::KW_TRUE:
            return "KW_TRUE";
        case GeneratedTokenKind::KW_FALSE:
            return "KW_FALSE";
        case GeneratedTokenKind::ID:
            return "ID";
        case GeneratedTokenKind::STRING:
            return "STRING";
        case GeneratedTokenKind::CHAR:
            return "CHAR";
        case GeneratedTokenKind::NUMBER:
            return "NUMBER";
        case GeneratedTokenKind::EQEQ:
            return "EQEQ";
        case GeneratedTokenKind::NEQ:
            return "NEQ";
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
        case GeneratedTokenKind::STAR:
            return "STAR";
        case GeneratedTokenKind::SLASH:
            return "SLASH";
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
        case GeneratedTokenKind::LPAREN:
            return "LPAREN";
        case GeneratedTokenKind::RPAREN:
            return "RPAREN";
        case GeneratedTokenKind::LBRACE:
            return "LBRACE";
        case GeneratedTokenKind::RBRACE:
            return "RBRACE";
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

std::string NumberToString(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
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
    throw InterpreterException(std::string("cannot use null value as number in ") + std::string(context));
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
    if (dynamic_cast<const gen::Pow*>(&expr) != nullptr) {
        return "pow";
    }
    if (dynamic_cast<const gen::Negate*>(&expr) != nullptr) {
        return "neg";
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
    return "expr";
}

std::string DescribeForInit(const gen::ForInit& init) {
    if (const auto* let_init = dynamic_cast<const gen::ForInitLet*>(&init)) {
        return "let " + let_init->name() + " = " + DescribeExpr(let_init->expr());
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
    return "stmt";
}

struct RuntimeEnvironment {
    SymbolTable symbols;
    std::unordered_map<std::string, Value> global_values;
    std::unordered_map<std::string, const gen::FunctionDecl*> functions;
    std::vector<std::unordered_map<std::string, Value>> local_frames;
};

bool IsBuiltinFunction(std::string_view name) {
    return name == "sin" || name == "cos" || name == "tan" || name == "sqrt" || name == "abs" || name == "exp" ||
           name == "ln" || name == "log10" || name == "pow" || name == "min" || name == "max" || name == "sum" ||
           name == "print" || name == "println" || name == "readln" || name == "input";
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
    for (auto it = env.local_frames.rbegin(); it != env.local_frames.rend(); ++it) {
        const auto local_it = it->find(std::string(name));
        if (local_it != it->end()) {
            return local_it->second;
        }
    }

    const auto global_it = env.global_values.find(std::string(name));
    if (global_it != env.global_values.end()) {
        return global_it->second;
    }
    if (env.symbols.globals.find(std::string(name)) != env.symbols.globals.end()) {
        throw InterpreterException("global variable declared but not initialized at use: " + std::string(name));
    }
    throw InterpreterException("unknown identifier: " + std::string(name));
}

void AssignVariable(std::string name, Value value, RuntimeEnvironment& env) {
    if (env.local_frames.empty()) {
        env.global_values[std::move(name)] = std::move(value);
    } else {
        env.local_frames.back()[std::move(name)] = std::move(value);
    }
}

struct StatementResult {
    bool returned = false;
    Value value = std::monostate{};
};

Value EvaluateExpr(const gen::Expr& expr, RuntimeEnvironment& env);
StatementResult ExecuteStatement(const gen::Statement& statement, RuntimeEnvironment& env);

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
                                     RuntimeEnvironment& env) {
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
    if (const auto* add = dynamic_cast<const gen::Add*>(&expr)) {
        const Value lhs = EvaluateExpr(add->lhs(), env);
        const Value rhs = EvaluateExpr(add->rhs(), env);
        if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs)) {
            return ValueToStringInternal(lhs) + ValueToStringInternal(rhs);
        }
        return AsNumber(lhs, "+") + AsNumber(rhs, "+");
    }
    if (const auto* sub = dynamic_cast<const gen::Subtract*>(&expr)) {
        return AsNumber(EvaluateExpr(sub->lhs(), env), "-") - AsNumber(EvaluateExpr(sub->rhs(), env), "-");
    }
    if (const auto* mul = dynamic_cast<const gen::Multiply*>(&expr)) {
        return AsNumber(EvaluateExpr(mul->lhs(), env), "*") * AsNumber(EvaluateExpr(mul->rhs(), env), "*");
    }
    if (const auto* div = dynamic_cast<const gen::Divide*>(&expr)) {
        const double rhs = AsNumber(EvaluateExpr(div->rhs(), env), "/");
        if (rhs == 0.0) {
            throw InterpreterException("division by zero");
        }
        return AsNumber(EvaluateExpr(div->lhs(), env), "/") / rhs;
    }
    if (const auto* pow_expr = dynamic_cast<const gen::Pow*>(&expr)) {
        return std::pow(AsNumber(EvaluateExpr(pow_expr->lhs(), env), "^") ,
                        AsNumber(EvaluateExpr(pow_expr->rhs(), env), "^"));
    }
    if (const auto* neg = dynamic_cast<const gen::Negate*>(&expr)) {
        return -AsNumber(EvaluateExpr(neg->expr(), env), "unary -");
    }
    if (const auto* eq = dynamic_cast<const gen::Equal*>(&expr)) {
        return ValueEquals(EvaluateExpr(eq->lhs(), env), EvaluateExpr(eq->rhs(), env));
    }
    if (const auto* neq = dynamic_cast<const gen::NotEqual*>(&expr)) {
        return !ValueEquals(EvaluateExpr(neq->lhs(), env), EvaluateExpr(neq->rhs(), env));
    }
    if (const auto* lt = dynamic_cast<const gen::Less*>(&expr)) {
        return ValueLess(EvaluateExpr(lt->lhs(), env), EvaluateExpr(lt->rhs(), env));
    }
    if (const auto* lte = dynamic_cast<const gen::LessEqual*>(&expr)) {
        const Value lhs = EvaluateExpr(lte->lhs(), env);
        const Value rhs = EvaluateExpr(lte->rhs(), env);
        return ValueLess(lhs, rhs) || ValueEquals(lhs, rhs);
    }
    if (const auto* gt = dynamic_cast<const gen::Greater*>(&expr)) {
        return ValueLess(EvaluateExpr(gt->rhs(), env), EvaluateExpr(gt->lhs(), env));
    }
    if (const auto* gte = dynamic_cast<const gen::GreaterEqual*>(&expr)) {
        const Value lhs = EvaluateExpr(gte->lhs(), env);
        const Value rhs = EvaluateExpr(gte->rhs(), env);
        return ValueLess(rhs, lhs) || ValueEquals(lhs, rhs);
    }
    if (const auto* ternary = dynamic_cast<const gen::Ternary*>(&expr)) {
        if (IsTruthy(EvaluateExpr(ternary->condition(), env))) {
            return EvaluateExpr(ternary->when_true(), env);
        }
        return EvaluateExpr(ternary->when_false(), env);
    }
    if (const auto* call = dynamic_cast<const gen::Call*>(&expr)) {
        const std::vector<Value> args = EvaluateArgs(call->args(), env);

        const auto user_it = env.functions.find(call->name());
        if (user_it != env.functions.end()) {
            const gen::FunctionDecl& function = *user_it->second;
            if (function.params().size() != args.size()) {
                throw InterpreterException("function '" + function.name() + "' expects " +
                                           std::to_string(function.params().size()) + " argument(s), got " +
                                           std::to_string(args.size()));
            }

            std::unordered_map<std::string, Value> frame;
            frame.reserve(function.params().size());
            for (std::size_t i = 0; i < function.params().size(); ++i) {
                frame[function.params()[i]] = args[i];
            }

            env.local_frames.push_back(std::move(frame));
            struct FrameGuard {
                RuntimeEnvironment& env_ref;
                ~FrameGuard() {
                    env_ref.local_frames.pop_back();
                }
            } guard{env};

            const StatementResult result = ExecuteStatementList(function.body(), env);
            return result.value;
        }

        if (IsBuiltinFunction(call->name())) {
            return CallBuiltin(call->name(), args);
        }

        throw InterpreterException("unknown function: " + call->name());
    }

    throw InterpreterException("unsupported expression node");
}

StatementResult ExecuteStatement(const gen::Statement& statement, RuntimeEnvironment& env) {
    if (const auto* let_stmt = dynamic_cast<const gen::LetStmt*>(&statement)) {
        Value value = EvaluateExpr(let_stmt->expr(), env);
        AssignVariable(let_stmt->name(), value, env);
        return StatementResult{.returned = false, .value = std::move(value)};
    }
    if (const auto* return_stmt = dynamic_cast<const gen::ReturnStmt*>(&statement)) {
        return StatementResult{.returned = true, .value = EvaluateExpr(return_stmt->expr(), env)};
    }
    if (const auto* expr_stmt = dynamic_cast<const gen::ExprStmt*>(&statement)) {
        return StatementResult{.returned = false, .value = EvaluateExpr(expr_stmt->expr(), env)};
    }
    if (const auto* if_stmt = dynamic_cast<const gen::IfStmt*>(&statement)) {
        if (IsTruthy(EvaluateExpr(if_stmt->condition(), env))) {
            return ExecuteStatementList(if_stmt->then_body(), env);
        }
        return ExecuteStatementList(if_stmt->else_body(), env);
    }
    if (const auto* while_stmt = dynamic_cast<const gen::WhileStmt*>(&statement)) {
        Value last = std::monostate{};
        while (IsTruthy(EvaluateExpr(while_stmt->condition(), env))) {
            const StatementResult body = ExecuteStatementList(while_stmt->body(), env);
            if (body.returned) {
                return body;
            }
            last = body.value;
        }
        return StatementResult{.returned = false, .value = std::move(last)};
    }
    if (const auto* for_stmt = dynamic_cast<const gen::ForStmt*>(&statement)) {
        ExecuteForInit(for_stmt->init(), env);

        Value last = std::monostate{};
        while (IsTruthy(EvaluateExpr(for_stmt->condition(), env))) {
            const StatementResult body = ExecuteStatementList(for_stmt->body(), env);
            if (body.returned) {
                return body;
            }
            last = body.value;
            ExecuteForInit(for_stmt->update(), env);
        }
        return StatementResult{.returned = false, .value = std::move(last)};
    }

    throw InterpreterException("unsupported statement node");
}

void CollectGlobalsFromStatement(const gen::Statement& statement, SymbolTable& symbols);

void CollectGlobalsFromForInit(const gen::ForInit& init, SymbolTable& symbols) {
    if (const auto* let_init = dynamic_cast<const gen::ForInitLet*>(&init)) {
        symbols.globals.insert(let_init->name());
        return;
    }
    if (dynamic_cast<const gen::ForInitExpr*>(&init) != nullptr) {
        return;
    }
}

void CollectGlobalsFromStatement(const gen::Statement& statement, SymbolTable& symbols) {
    if (const auto* let_stmt = dynamic_cast<const gen::LetStmt*>(&statement)) {
        symbols.globals.insert(let_stmt->name());
        return;
    }
    if (const auto* if_stmt = dynamic_cast<const gen::IfStmt*>(&statement)) {
        for (const std::unique_ptr<gen::Statement>& stmt : if_stmt->then_body()) {
            if (stmt != nullptr) {
                CollectGlobalsFromStatement(*stmt, symbols);
            }
        }
        for (const std::unique_ptr<gen::Statement>& stmt : if_stmt->else_body()) {
            if (stmt != nullptr) {
                CollectGlobalsFromStatement(*stmt, symbols);
            }
        }
        return;
    }
    if (const auto* while_stmt = dynamic_cast<const gen::WhileStmt*>(&statement)) {
        for (const std::unique_ptr<gen::Statement>& stmt : while_stmt->body()) {
            if (stmt != nullptr) {
                CollectGlobalsFromStatement(*stmt, symbols);
            }
        }
        return;
    }
    if (const auto* for_stmt = dynamic_cast<const gen::ForStmt*>(&statement)) {
        CollectGlobalsFromForInit(for_stmt->init(), symbols);
        for (const std::unique_ptr<gen::Statement>& stmt : for_stmt->body()) {
            if (stmt != nullptr) {
                CollectGlobalsFromStatement(*stmt, symbols);
            }
        }
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
            out.symbols.functions.insert(function->name());
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

        if (const auto* top_stmt = dynamic_cast<const gen::TopStatement*>(item.get())) {
            CollectGlobalsFromStatement(top_stmt->statement(), out.symbols);
            out.flattened_items.push_back(DescribeStatement(top_stmt->statement()));
            continue;
        }
    }

    return out;
}

Value InterpretProgram(const AST& ast) {
    const gen::Program& program = RequireProgram(ast);

    RuntimeEnvironment env;
    env.symbols = AnnotateProgram(ast).symbols;
    env.global_values["pi"] = std::acos(-1.0);
    env.global_values["e"] = std::exp(1.0);
    env.global_values["tau"] = 2.0 * std::acos(-1.0);
    env.global_values["true"] = true;
    env.global_values["false"] = false;

    for (const std::unique_ptr<gen::Item>& item : program.items().items()) {
        if (const auto* function = dynamic_cast<const gen::FunctionDecl*>(item.get())) {
            if (!env.functions.emplace(function->name(), function).second) {
                throw InterpreterException("duplicate function declaration: " + function->name());
            }
        }
    }

    Value last_value = std::monostate{};
    for (const std::unique_ptr<gen::Item>& item : program.items().items()) {
        if (const auto* top_stmt = dynamic_cast<const gen::TopStatement*>(item.get())) {
            const StatementResult result = ExecuteStatement(top_stmt->statement(), env);
            last_value = result.value;
            if (result.returned) {
                return last_value;
            }
        }
    }

    return last_value;
}

Value InterpretSource(std::string_view source_text) {
    const AST ast = ParseProgram(source_text);
    return InterpretProgram(ast);
}

} // namespace compiler::interpreter
