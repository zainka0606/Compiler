#include "Common/FileIO.h"
#include "Common/NumberParsing.h"
#include "Interpreter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
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

namespace gen = generated::Neon::ast;

const gen::Program &RequireProgram(const AST &ast) {
    if (ast.Empty()) {
        throw InterpreterException("program AST is empty");
    }
    const auto *program = dynamic_cast<const gen::Program *>(&ast.Root());
    if (program == nullptr) {
        throw InterpreterException("program AST root is not Program");
    }
    return *program;
}

double ParseNumberFromText(std::string_view text, std::string_view context) {
    try {
        return compiler::common::ParseNumericLiteral(text, context);
    } catch (const std::exception &ex) {
        throw InterpreterException(ex.what());
    }
}

std::string_view TrimWhitespace(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
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
            throw InterpreterException(std::string("dangling escape in ") +
                                       std::string(context));
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
    }
    return out;
}

std::string ParseQuotedLiteral(std::string_view text, char quote,
                               std::string_view context) {
    if (text.size() < 2 || text.front() != quote || text.back() != quote) {
        throw InterpreterException(std::string("invalid ") +
                                   std::string(context) +
                                   " literal: " + std::string(text));
    }
    return DecodeEscapedText(text.substr(1, text.size() - 2), context);
}

class IRFunctionCodegen {
  public:
    explicit IRFunctionCodegen(ir::Function &function,
                               bool module_scope_globals = false)
        : function_(&function), builder_(function),
          module_scope_globals_(module_scope_globals),
          last_value_temp_(builder_.NewTemp()) {
        scopes_.emplace_back();
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            scopes_.front()[function.params[i]] = function.param_slots[i];
        }
        if (function.is_method) {
            for (ir::SlotId slot = 0; slot < function.slot_names.size();
                 ++slot) {
                if (function.slot_names[slot] == "self") {
                    scopes_.front()["self"] = slot;
                    break;
                }
            }
        }
        builder_.Emit(
            ir::MoveInst{.dst = last_value_temp_, .src = ir::ValueRef::Null()});
    }

    void EmitStatementList(
        const std::vector<std::unique_ptr<gen::Statement>> &statements) {
        for (const std::unique_ptr<gen::Statement> &statement : statements) {
            if (!statement) {
                continue;
            }
            if (builder_.HasTerminator(builder_.Current())) {
                return;
            }
            EmitStatement(*statement);
        }
    }

    void Finalize() {
        if (!builder_.HasTerminator(builder_.Current())) {
            builder_.SetTerminator(
                ir::ReturnTerm{.value = ir::ValueRef::Temp(last_value_temp_)});
        }
    }

    void EmitTopLevelStatement(const gen::Statement &statement) {
        if (builder_.HasTerminator(builder_.Current())) {
            return;
        }
        EmitStatement(statement);
    }

  private:
    struct LValue {
        enum class Kind {
            Local,
            Global,
            Member,
            Index,
        } kind = Kind::Local;

        ir::SlotId local_slot = ir::kInvalidSlot;
        std::string name;
        ir::ValueRef object;
        ir::ValueRef index;
    };

    ir::Function *function_ = nullptr;
    ir::FunctionBuilder builder_;
    std::vector<std::unordered_map<std::string, ir::SlotId>> scopes_;
    bool module_scope_globals_ = false;
    ir::LocalId last_value_temp_;
    std::vector<ir::BlockId> break_targets_;

    [[nodiscard]] bool IsModuleScope() const {
        return module_scope_globals_ && scopes_.size() == 1;
    }

    void EnterScope() { scopes_.emplace_back(); }

    void ExitScope() {
        if (scopes_.size() <= 1) {
            throw InterpreterException(
                "internal scope underflow in IR codegen");
        }
        scopes_.pop_back();
    }

    [[nodiscard]] std::optional<ir::SlotId>
    ResolveLocalSlot(std::string_view name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(std::string(name));
            if (found != it->end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] ir::SlotId DeclareLocalSlot(const std::string &name,
                                              const std::string &type_name) {
        const ir::SlotId slot = builder_.NewSlot(name);
        scopes_.back()[name] = slot;
        if (function_ != nullptr && slot < function_->slot_types.size()) {
            function_->slot_types[slot] = type_name;
        }
        return slot;
    }

    void EmitDeclaration(const std::string &name, const std::string &type_name,
                         const ir::ValueRef &value) {
        if (IsModuleScope()) {
            builder_.Emit(ir::DeclareGlobalInst{
                .name = name,
                .type_name = type_name,
                .value = value,
            });
            return;
        }
        const ir::SlotId slot = DeclareLocalSlot(name, type_name);
        builder_.Emit(ir::StoreLocalInst{.slot = slot, .value = value});
    }

    void SetLast(const ir::ValueRef &value) {
        builder_.Emit(ir::MoveInst{.dst = last_value_temp_, .src = value});
    }

    void SetLastNull() { SetLast(ir::ValueRef::Null()); }

    void EmitJumpIfNotTerminated(ir::BlockId target) {
        if (!builder_.HasTerminator(builder_.Current())) {
            builder_.SetTerminator(ir::JumpTerm{.target = target});
        }
    }

    ir::ValueRef EmitUnary(ir::UnaryOp op, ir::ValueRef value) {
        const ir::LocalId dst = builder_.NewTemp();
        builder_.Emit(ir::UnaryInst{.dst = dst, .op = op, .value = value});
        return ir::ValueRef::Temp(dst);
    }

    ir::ValueRef EmitBinary(ir::BinaryOp op, ir::ValueRef lhs,
                            ir::ValueRef rhs) {
        const ir::LocalId dst = builder_.NewTemp();
        builder_.Emit(
            ir::BinaryInst{.dst = dst, .op = op, .lhs = lhs, .rhs = rhs});
        return ir::ValueRef::Temp(dst);
    }

    ir::ValueRef EmitTruthiness(ir::ValueRef value) {
        const ir::ValueRef neg = EmitUnary(ir::UnaryOp::LogicalNot, value);
        return EmitUnary(ir::UnaryOp::LogicalNot, neg);
    }

    ir::ValueRef EmitObjectLoadMember(ir::ValueRef object,
                                      const std::string &member) {
        const ir::LocalId offset = builder_.NewTemp();
        builder_.Emit(ir::ResolveFieldOffsetInst{
            .dst = offset,
            .object = object,
            .member = member,
        });
        const ir::LocalId dst = builder_.NewTemp();
        builder_.Emit(ir::ObjectLoadInst{
            .dst = dst,
            .object = object,
            .offset = ir::ValueRef::Temp(offset),
        });
        return ir::ValueRef::Temp(dst);
    }

    void EmitObjectStoreMember(ir::ValueRef object, const std::string &member,
                               ir::ValueRef value) {
        const ir::LocalId offset = builder_.NewTemp();
        builder_.Emit(ir::ResolveFieldOffsetInst{
            .dst = offset,
            .object = object,
            .member = member,
        });
        builder_.Emit(ir::ObjectStoreInst{
            .object = object,
            .offset = ir::ValueRef::Temp(offset),
            .value = value,
        });
    }

    ir::ValueRef EmitVirtualMethodCall(ir::ValueRef object,
                                       const std::string &method,
                                       std::vector<ir::ValueRef> args) {
        const ir::LocalId slot = builder_.NewTemp();
        builder_.Emit(ir::ResolveMethodSlotInst{
            .dst = slot,
            .object = object,
            .method = method,
        });
        const ir::LocalId dst = builder_.NewTemp();
        builder_.Emit(ir::VirtualCallInst{
            .dst = dst,
            .object = object,
            .slot = ir::ValueRef::Temp(slot),
            .args = std::move(args),
        });
        return ir::ValueRef::Temp(dst);
    }

    LValue EmitLValue(const gen::Expr &target) {
        if (const auto *identifier =
                dynamic_cast<const gen::Identifier *>(&target)) {
            if (const std::optional<ir::SlotId> slot =
                    ResolveLocalSlot(identifier->name())) {
                return LValue{.kind = LValue::Kind::Local,
                              .local_slot = *slot,
                              .name = identifier->name()};
            }
            return LValue{.kind = LValue::Kind::Global,
                          .name = identifier->name()};
        }

        if (const auto *member =
                dynamic_cast<const gen::MemberAccess *>(&target)) {
            return LValue{.kind = LValue::Kind::Member,
                          .name = member->member(),
                          .object = EmitExpr(member->object())};
        }

        if (const auto *index =
                dynamic_cast<const gen::IndexAccess *>(&target)) {
            return LValue{.kind = LValue::Kind::Index,
                          .object = EmitExpr(index->object()),
                          .index = EmitExpr(index->index())};
        }

        throw InterpreterException("invalid assignment target");
    }

    ir::ValueRef LoadLValue(const LValue &value) {
        if (value.kind == LValue::Kind::Local) {
            const ir::LocalId dst = builder_.NewTemp();
            builder_.Emit(
                ir::LoadLocalInst{.dst = dst, .slot = value.local_slot});
            return ir::ValueRef::Temp(dst);
        }
        if (value.kind == LValue::Kind::Global) {
            const ir::LocalId dst = builder_.NewTemp();
            builder_.Emit(ir::LoadGlobalInst{.dst = dst, .name = value.name});
            return ir::ValueRef::Temp(dst);
        }
        if (value.kind == LValue::Kind::Member) {
            return EmitObjectLoadMember(value.object, value.name);
        }
        const ir::LocalId dst = builder_.NewTemp();
        builder_.Emit(ir::ArrayLoadInst{
            .dst = dst, .array = value.object, .index = value.index});
        return ir::ValueRef::Temp(dst);
    }

    void StoreLValue(const LValue &target, ir::ValueRef value) {
        if (target.kind == LValue::Kind::Local) {
            builder_.Emit(
                ir::StoreLocalInst{.slot = target.local_slot, .value = value});
            return;
        }
        if (target.kind == LValue::Kind::Global) {
            builder_.Emit(
                ir::StoreGlobalInst{.name = target.name, .value = value});
            return;
        }
        if (target.kind == LValue::Kind::Member) {
            EmitObjectStoreMember(target.object, target.name, value);
            return;
        }
        builder_.Emit(ir::ArrayStoreInst{
            .array = target.object, .index = target.index, .value = value});
    }

    ir::BinaryOp BinaryOpForCompound(std::string_view op) {
        if (op == "+=") {
            return ir::BinaryOp::Add;
        }
        if (op == "-=") {
            return ir::BinaryOp::Subtract;
        }
        if (op == "*=") {
            return ir::BinaryOp::Multiply;
        }
        if (op == "/=") {
            return ir::BinaryOp::Divide;
        }
        if (op == "%=") {
            return ir::BinaryOp::Modulo;
        }
        if (op == "&=") {
            return ir::BinaryOp::BitwiseAnd;
        }
        if (op == "|=") {
            return ir::BinaryOp::BitwiseOr;
        }
        if (op == "^=") {
            return ir::BinaryOp::BitwiseXor;
        }
        if (op == "<<=") {
            return ir::BinaryOp::ShiftLeft;
        }
        if (op == ">>=") {
            return ir::BinaryOp::ShiftRight;
        }

        throw InterpreterException(
            "unsupported compound assignment operator: " + std::string(op));
    }

    ir::ValueRef EmitEmbeddedExpression(std::string_view expression_text) {
        const std::string trimmed =
            std::string(TrimWhitespace(expression_text));
        if (trimmed.empty()) {
            throw InterpreterException("empty formatted-string expression");
        }

        const AST embedded_ast = ParseProgram(trimmed + ";");
        const gen::Program &program = RequireProgram(embedded_ast);
        const auto &items = program.items().items();
        if (items.size() != 1 || items.front() == nullptr) {
            throw InterpreterException("formatted-string expression must parse "
                                       "as one expression statement: " +
                                       trimmed);
        }

        const auto *top_statement =
            dynamic_cast<const gen::TopStatement *>(items.front().get());
        if (top_statement == nullptr) {
            throw InterpreterException(
                "formatted-string expression did not parse as a statement: " +
                trimmed);
        }

        const auto *expr_statement =
            dynamic_cast<const gen::ExprStmt *>(&top_statement->statement());
        if (expr_statement == nullptr) {
            throw InterpreterException(
                "formatted-string expression must be an expression: " +
                trimmed);
        }

        return EmitExpr(expr_statement->expr());
    }

    ir::ValueRef EmitFormattedString(std::string_view literal) {
        if (literal.size() < 3 || literal[0] != 'f' || literal[1] != '"' ||
            literal.back() != '"') {
            throw InterpreterException("invalid formatted string literal: " +
                                       std::string(literal));
        }

        const std::string_view body = literal.substr(2, literal.size() - 3);

        ir::LocalId out_temp = builder_.NewTemp();
        builder_.Emit(
            ir::MoveInst{.dst = out_temp, .src = ir::ValueRef::String("")});

        std::function<void(const ir::ValueRef &)> append_part =
            [&](const ir::ValueRef &part) {
                const ir::LocalId next = builder_.NewTemp();
                builder_.Emit(ir::BinaryInst{
                    .dst = next,
                    .op = ir::BinaryOp::Add,
                    .lhs = ir::ValueRef::Temp(out_temp),
                    .rhs = part,
                });
                out_temp = next;
            };

        std::string pending_text;
        pending_text.reserve(body.size());

        const auto flush_text = [&]() {
            if (pending_text.empty()) {
                return;
            }
            append_part(ir::ValueRef::String(pending_text));
            pending_text.clear();
        };

        std::size_t i = 0;
        while (i < body.size()) {
            const char c = body[i];
            if (c == '\\') {
                if (i + 1 >= body.size()) {
                    throw InterpreterException(
                        "dangling escape in formatted string");
                }
                const char next = body[++i];
                switch (next) {
                case 'n':
                    pending_text.push_back('\n');
                    break;
                case 't':
                    pending_text.push_back('\t');
                    break;
                case 'r':
                    pending_text.push_back('\r');
                    break;
                case '0':
                    pending_text.push_back('\0');
                    break;
                case '\\':
                    pending_text.push_back('\\');
                    break;
                case '"':
                    pending_text.push_back('"');
                    break;
                case '\'':
                    pending_text.push_back('\'');
                    break;
                default:
                    pending_text.push_back(next);
                    break;
                }
                ++i;
                continue;
            }

            if (c == '{') {
                if (i + 1 < body.size() && body[i + 1] == '{') {
                    pending_text.push_back('{');
                    i += 2;
                    continue;
                }

                flush_text();

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

                if (close >= body.size() || depth != 0) {
                    throw InterpreterException(
                        "unterminated expression in formatted string");
                }

                const std::string_view expression_view =
                    body.substr(i + 1, close - (i + 1));
                append_part(EmitEmbeddedExpression(expression_view));
                i = close + 1;
                continue;
            }

            if (c == '}') {
                if (i + 1 < body.size() && body[i + 1] == '}') {
                    pending_text.push_back('}');
                    i += 2;
                    continue;
                }
                throw InterpreterException(
                    "single '}' in formatted string must be escaped as '}}'");
            }

            pending_text.push_back(c);
            ++i;
        }

        flush_text();

        return ir::ValueRef::Temp(out_temp);
    }

    ir::ValueRef EmitExpr(const gen::Expr &expr) {
        if (const auto *number = dynamic_cast<const gen::Number *>(&expr)) {
            return ir::ValueRef::Number(
                ParseNumberFromText(number->value(), "number literal"));
        }
        if (const auto *formatted_string =
                dynamic_cast<const gen::FormattedString *>(&expr)) {
            return EmitFormattedString(formatted_string->value());
        }
        if (const auto *string_literal =
                dynamic_cast<const gen::StringLiteral *>(&expr)) {
            return ir::ValueRef::String(
                ParseQuotedLiteral(string_literal->value(), '"', "string"));
        }
        if (const auto *char_literal =
                dynamic_cast<const gen::CharLiteral *>(&expr)) {
            const std::string decoded =
                ParseQuotedLiteral(char_literal->value(), '\'', "char");
            if (decoded.size() != 1) {
                throw InterpreterException(
                    "char literal must decode to exactly one character");
            }
            return ir::ValueRef::Char(decoded[0]);
        }
        if (const auto *bool_literal =
                dynamic_cast<const gen::BoolLiteral *>(&expr)) {
            if (bool_literal->value() == "true") {
                return ir::ValueRef::Bool(true);
            }
            if (bool_literal->value() == "false") {
                return ir::ValueRef::Bool(false);
            }
            throw InterpreterException("invalid bool literal: " +
                                       bool_literal->value());
        }
        if (const auto *identifier =
                dynamic_cast<const gen::Identifier *>(&expr)) {
            const ir::LocalId dst = builder_.NewTemp();
            if (const std::optional<ir::SlotId> slot =
                    ResolveLocalSlot(identifier->name())) {
                builder_.Emit(ir::LoadLocalInst{.dst = dst, .slot = *slot});
            } else {
                builder_.Emit(
                    ir::LoadGlobalInst{.dst = dst, .name = identifier->name()});
            }
            return ir::ValueRef::Temp(dst);
        }
        if (const auto *let_expr = dynamic_cast<const gen::LetExpr *>(&expr)) {
            const ir::ValueRef value = EmitExpr(let_expr->expr());
            EmitDeclaration(let_expr->name(), let_expr->type_name(), value);
            return value;
        }
        if (const auto *assign_expr =
                dynamic_cast<const gen::AssignExpr *>(&expr)) {
            const LValue target = EmitLValue(assign_expr->target());
            const ir::ValueRef value = EmitExpr(assign_expr->expr());
            StoreLValue(target, value);
            return value;
        }
        if (const auto *compound_assign =
                dynamic_cast<const gen::CompoundAssignExpr *>(&expr)) {
            const LValue target = EmitLValue(compound_assign->target());
            const ir::ValueRef current = LoadLValue(target);
            const ir::ValueRef rhs = EmitExpr(compound_assign->expr());
            const ir::ValueRef result = EmitBinary(
                BinaryOpForCompound(compound_assign->op()), current, rhs);
            StoreLValue(target, result);
            return result;
        }
        if (const auto *cast_expr =
                dynamic_cast<const gen::CastExpr *>(&expr)) {
            const ir::LocalId dst = builder_.NewTemp();
            std::vector<ir::ValueRef> args;
            args.reserve(2);
            args.push_back(EmitExpr(cast_expr->expr()));
            args.push_back(ir::ValueRef::String(cast_expr->type_name()));
            builder_.Emit(ir::CallInst{
                .dst = dst,
                .callee = "__cast",
                .args = std::move(args),
            });
            return ir::ValueRef::Temp(dst);
        }
        if (const auto *array_literal =
                dynamic_cast<const gen::ArrayLiteral *>(&expr)) {
            const ir::LocalId dst = builder_.NewTemp();
            std::vector<ir::ValueRef> elements;
            elements.reserve(array_literal->elements().size());
            for (const std::unique_ptr<gen::Expr> &element :
                 array_literal->elements()) {
                if (!element) {
                    throw InterpreterException("null element in array literal");
                }
                elements.push_back(EmitExpr(*element));
            }
            builder_.Emit(ir::MakeArrayInst{
                .dst = dst,
                .elements = std::move(elements),
            });
            return ir::ValueRef::Temp(dst);
        }
        if (const auto *index = dynamic_cast<const gen::IndexAccess *>(&expr)) {
            const ir::LocalId dst = builder_.NewTemp();
            builder_.Emit(ir::ArrayLoadInst{.dst = dst,
                                            .array = EmitExpr(index->object()),
                                            .index = EmitExpr(index->index())});
            return ir::ValueRef::Temp(dst);
        }
        if (const auto *logical_or =
                dynamic_cast<const gen::LogicalOr *>(&expr)) {
            const ir::LocalId result = builder_.NewTemp();
            const ir::ValueRef lhs = EmitExpr(logical_or->lhs());

            const ir::BlockId lhs_true = builder_.CreateBlock("lor.true");
            const ir::BlockId rhs_block = builder_.CreateBlock("lor.rhs");
            const ir::BlockId end_block = builder_.CreateBlock("lor.end");

            builder_.SetTerminator(ir::BranchTerm{.condition = lhs,
                                                  .true_target = lhs_true,
                                                  .false_target = rhs_block});

            builder_.SetCurrent(lhs_true);
            builder_.Emit(
                ir::MoveInst{.dst = result, .src = ir::ValueRef::Bool(true)});
            builder_.SetTerminator(ir::JumpTerm{.target = end_block});

            builder_.SetCurrent(rhs_block);
            const ir::ValueRef rhs_bool =
                EmitTruthiness(EmitExpr(logical_or->rhs()));
            builder_.Emit(ir::MoveInst{.dst = result, .src = rhs_bool});
            builder_.SetTerminator(ir::JumpTerm{.target = end_block});

            builder_.SetCurrent(end_block);
            return ir::ValueRef::Temp(result);
        }
        if (const auto *logical_and =
                dynamic_cast<const gen::LogicalAnd *>(&expr)) {
            const ir::LocalId result = builder_.NewTemp();
            const ir::ValueRef lhs = EmitExpr(logical_and->lhs());

            const ir::BlockId lhs_false = builder_.CreateBlock("land.false");
            const ir::BlockId rhs_block = builder_.CreateBlock("land.rhs");
            const ir::BlockId end_block = builder_.CreateBlock("land.end");

            builder_.SetTerminator(ir::BranchTerm{.condition = lhs,
                                                  .true_target = rhs_block,
                                                  .false_target = lhs_false});

            builder_.SetCurrent(lhs_false);
            builder_.Emit(
                ir::MoveInst{.dst = result, .src = ir::ValueRef::Bool(false)});
            builder_.SetTerminator(ir::JumpTerm{.target = end_block});

            builder_.SetCurrent(rhs_block);
            const ir::ValueRef rhs_bool =
                EmitTruthiness(EmitExpr(logical_and->rhs()));
            builder_.Emit(ir::MoveInst{.dst = result, .src = rhs_bool});
            builder_.SetTerminator(ir::JumpTerm{.target = end_block});

            builder_.SetCurrent(end_block);
            return ir::ValueRef::Temp(result);
        }
        if (const auto *add = dynamic_cast<const gen::Add *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Add, EmitExpr(add->lhs()),
                              EmitExpr(add->rhs()));
        }
        if (const auto *sub = dynamic_cast<const gen::Subtract *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Subtract, EmitExpr(sub->lhs()),
                              EmitExpr(sub->rhs()));
        }
        if (const auto *mul = dynamic_cast<const gen::Multiply *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Multiply, EmitExpr(mul->lhs()),
                              EmitExpr(mul->rhs()));
        }
        if (const auto *div = dynamic_cast<const gen::Divide *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Divide, EmitExpr(div->lhs()),
                              EmitExpr(div->rhs()));
        }
        if (const auto *idiv = dynamic_cast<const gen::IntDivide *>(&expr)) {
            return EmitBinary(ir::BinaryOp::IntDivide, EmitExpr(idiv->lhs()),
                              EmitExpr(idiv->rhs()));
        }
        if (const auto *mod = dynamic_cast<const gen::Modulo *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Modulo, EmitExpr(mod->lhs()),
                              EmitExpr(mod->rhs()));
        }
        if (const auto *pow = dynamic_cast<const gen::Pow *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Pow, EmitExpr(pow->lhs()),
                              EmitExpr(pow->rhs()));
        }
        if (const auto *neg = dynamic_cast<const gen::Negate *>(&expr)) {
            return EmitUnary(ir::UnaryOp::Negate, EmitExpr(neg->expr()));
        }
        if (const auto *logical_not =
                dynamic_cast<const gen::LogicalNot *>(&expr)) {
            return EmitUnary(ir::UnaryOp::LogicalNot,
                             EmitExpr(logical_not->expr()));
        }
        if (const auto *bitwise_not =
                dynamic_cast<const gen::BitwiseNot *>(&expr)) {
            return EmitUnary(ir::UnaryOp::BitwiseNot,
                             EmitExpr(bitwise_not->expr()));
        }
        if (const auto *bitwise_and =
                dynamic_cast<const gen::BitwiseAnd *>(&expr)) {
            return EmitBinary(ir::BinaryOp::BitwiseAnd,
                              EmitExpr(bitwise_and->lhs()),
                              EmitExpr(bitwise_and->rhs()));
        }
        if (const auto *bitwise_or =
                dynamic_cast<const gen::BitwiseOr *>(&expr)) {
            return EmitBinary(ir::BinaryOp::BitwiseOr,
                              EmitExpr(bitwise_or->lhs()),
                              EmitExpr(bitwise_or->rhs()));
        }
        if (const auto *bitwise_xor =
                dynamic_cast<const gen::BitwiseXor *>(&expr)) {
            return EmitBinary(ir::BinaryOp::BitwiseXor,
                              EmitExpr(bitwise_xor->lhs()),
                              EmitExpr(bitwise_xor->rhs()));
        }
        if (const auto *shift_left =
                dynamic_cast<const gen::ShiftLeft *>(&expr)) {
            return EmitBinary(ir::BinaryOp::ShiftLeft,
                              EmitExpr(shift_left->lhs()),
                              EmitExpr(shift_left->rhs()));
        }
        if (const auto *shift_right =
                dynamic_cast<const gen::ShiftRight *>(&expr)) {
            return EmitBinary(ir::BinaryOp::ShiftRight,
                              EmitExpr(shift_right->lhs()),
                              EmitExpr(shift_right->rhs()));
        }
        if (const auto *eq = dynamic_cast<const gen::Equal *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Equal, EmitExpr(eq->lhs()),
                              EmitExpr(eq->rhs()));
        }
        if (const auto *neq = dynamic_cast<const gen::NotEqual *>(&expr)) {
            return EmitBinary(ir::BinaryOp::NotEqual, EmitExpr(neq->lhs()),
                              EmitExpr(neq->rhs()));
        }
        if (const auto *lt = dynamic_cast<const gen::Less *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Less, EmitExpr(lt->lhs()),
                              EmitExpr(lt->rhs()));
        }
        if (const auto *lte = dynamic_cast<const gen::LessEqual *>(&expr)) {
            return EmitBinary(ir::BinaryOp::LessEqual, EmitExpr(lte->lhs()),
                              EmitExpr(lte->rhs()));
        }
        if (const auto *gt = dynamic_cast<const gen::Greater *>(&expr)) {
            return EmitBinary(ir::BinaryOp::Greater, EmitExpr(gt->lhs()),
                              EmitExpr(gt->rhs()));
        }
        if (const auto *gte = dynamic_cast<const gen::GreaterEqual *>(&expr)) {
            return EmitBinary(ir::BinaryOp::GreaterEqual, EmitExpr(gte->lhs()),
                              EmitExpr(gte->rhs()));
        }
        if (const auto *ternary = dynamic_cast<const gen::Ternary *>(&expr)) {
            const ir::LocalId result = builder_.NewTemp();
            const ir::ValueRef condition = EmitExpr(ternary->condition());

            const ir::BlockId true_block = builder_.CreateBlock("ternary.true");
            const ir::BlockId false_block =
                builder_.CreateBlock("ternary.false");
            const ir::BlockId end_block = builder_.CreateBlock("ternary.end");

            builder_.SetTerminator(ir::BranchTerm{.condition = condition,
                                                  .true_target = true_block,
                                                  .false_target = false_block});

            builder_.SetCurrent(true_block);
            builder_.Emit(ir::MoveInst{.dst = result,
                                       .src = EmitExpr(ternary->when_true())});
            builder_.SetTerminator(ir::JumpTerm{.target = end_block});

            builder_.SetCurrent(false_block);
            builder_.Emit(ir::MoveInst{.dst = result,
                                       .src = EmitExpr(ternary->when_false())});
            builder_.SetTerminator(ir::JumpTerm{.target = end_block});

            builder_.SetCurrent(end_block);
            return ir::ValueRef::Temp(result);
        }
        if (const auto *method_call =
                dynamic_cast<const gen::MethodCall *>(&expr)) {
            const ir::ValueRef object = EmitExpr(method_call->object());
            std::vector<ir::ValueRef> args;
            args.reserve(method_call->args().size());
            for (const std::unique_ptr<gen::Expr> &arg : method_call->args()) {
                if (!arg) {
                    throw InterpreterException("null call argument node");
                }
                args.push_back(EmitExpr(*arg));
            }
            return EmitVirtualMethodCall(object, method_call->name(),
                                         std::move(args));
        }
        if (const auto *member =
                dynamic_cast<const gen::MemberAccess *>(&expr)) {
            return EmitObjectLoadMember(EmitExpr(member->object()),
                                        member->member());
        }
        if (const auto *call = dynamic_cast<const gen::Call *>(&expr)) {
            const ir::LocalId dst = builder_.NewTemp();
            std::vector<ir::ValueRef> args;
            args.reserve(call->args().size());
            for (const std::unique_ptr<gen::Expr> &arg : call->args()) {
                if (!arg) {
                    throw InterpreterException("null call argument node");
                }
                args.push_back(EmitExpr(*arg));
            }
            builder_.Emit(ir::CallInst{
                .dst = dst, .callee = call->name(), .args = std::move(args)});
            return ir::ValueRef::Temp(dst);
        }
        if (const auto *new_expr = dynamic_cast<const gen::NewExpr *>(&expr)) {
            const ir::LocalId dst = builder_.NewTemp();
            std::vector<ir::ValueRef> args;
            args.reserve(new_expr->args().size() + 1);
            args.push_back(ir::ValueRef::String(new_expr->name()));
            for (const std::unique_ptr<gen::Expr> &arg : new_expr->args()) {
                if (!arg) {
                    throw InterpreterException(
                        "null constructor argument node");
                }
                args.push_back(EmitExpr(*arg));
            }
            builder_.Emit(ir::CallInst{
                .dst = dst,
                .callee = "__new",
                .args = std::move(args),
            });
            return ir::ValueRef::Temp(dst);
        }

        throw InterpreterException(
            "unsupported expression node in IR lowering");
    }

    void EmitStatement(const gen::Statement &statement) {
        if (const auto *let_stmt =
                dynamic_cast<const gen::LetStmt *>(&statement)) {
            const ir::ValueRef value = EmitExpr(let_stmt->expr());
            EmitDeclaration(let_stmt->name(), let_stmt->type_name(), value);
            SetLast(value);
            return;
        }
        if (const auto *return_stmt =
                dynamic_cast<const gen::ReturnStmt *>(&statement)) {
            builder_.SetTerminator(
                ir::ReturnTerm{.value = EmitExpr(return_stmt->expr())});
            return;
        }
        if (dynamic_cast<const gen::BreakStmt *>(&statement) != nullptr) {
            if (break_targets_.empty()) {
                throw InterpreterException("break used outside loop or switch");
            }
            builder_.SetTerminator(
                ir::JumpTerm{.target = break_targets_.back()});
            return;
        }
        if (const auto *expr_stmt =
                dynamic_cast<const gen::ExprStmt *>(&statement)) {
            const ir::ValueRef value = EmitExpr(expr_stmt->expr());
            SetLast(value);
            return;
        }
        if (const auto *if_stmt =
                dynamic_cast<const gen::IfStmt *>(&statement)) {
            SetLastNull();
            const ir::ValueRef condition = EmitExpr(if_stmt->condition());
            const ir::BlockId then_block = builder_.CreateBlock("if.then");
            const ir::BlockId else_block = builder_.CreateBlock("if.else");
            const ir::BlockId end_block = builder_.CreateBlock("if.end");

            builder_.SetTerminator(ir::BranchTerm{.condition = condition,
                                                  .true_target = then_block,
                                                  .false_target = else_block});

            builder_.SetCurrent(then_block);
            EnterScope();
            EmitStatementList(if_stmt->then_body());
            ExitScope();
            EmitJumpIfNotTerminated(end_block);

            builder_.SetCurrent(else_block);
            EnterScope();
            EmitStatementList(if_stmt->else_body());
            ExitScope();
            EmitJumpIfNotTerminated(end_block);

            builder_.SetCurrent(end_block);
            return;
        }
        if (const auto *while_stmt =
                dynamic_cast<const gen::WhileStmt *>(&statement)) {
            SetLastNull();
            const ir::BlockId cond_block = builder_.CreateBlock("while.cond");
            const ir::BlockId body_block = builder_.CreateBlock("while.body");
            const ir::BlockId end_block = builder_.CreateBlock("while.end");

            builder_.SetTerminator(ir::JumpTerm{.target = cond_block});

            builder_.SetCurrent(cond_block);
            const ir::ValueRef condition = EmitExpr(while_stmt->condition());
            builder_.SetTerminator(ir::BranchTerm{.condition = condition,
                                                  .true_target = body_block,
                                                  .false_target = end_block});

            break_targets_.push_back(end_block);
            builder_.SetCurrent(body_block);
            EnterScope();
            EmitStatementList(while_stmt->body());
            ExitScope();
            EmitJumpIfNotTerminated(cond_block);
            break_targets_.pop_back();

            builder_.SetCurrent(end_block);
            return;
        }
        if (const auto *for_stmt =
                dynamic_cast<const gen::ForStmt *>(&statement)) {
            SetLastNull();
            EnterScope();
            (void)EmitExpr(for_stmt->init());

            const ir::BlockId cond_block = builder_.CreateBlock("for.cond");
            const ir::BlockId body_block = builder_.CreateBlock("for.body");
            const ir::BlockId update_block = builder_.CreateBlock("for.update");
            const ir::BlockId end_block = builder_.CreateBlock("for.end");

            builder_.SetTerminator(ir::JumpTerm{.target = cond_block});

            builder_.SetCurrent(cond_block);
            const ir::ValueRef condition = EmitExpr(for_stmt->condition());
            builder_.SetTerminator(ir::BranchTerm{.condition = condition,
                                                  .true_target = body_block,
                                                  .false_target = end_block});

            break_targets_.push_back(end_block);
            builder_.SetCurrent(body_block);
            EnterScope();
            EmitStatementList(for_stmt->body());
            ExitScope();
            EmitJumpIfNotTerminated(update_block);
            break_targets_.pop_back();

            builder_.SetCurrent(update_block);
            (void)EmitExpr(for_stmt->update());
            builder_.SetTerminator(ir::JumpTerm{.target = cond_block});

            builder_.SetCurrent(end_block);
            ExitScope();
            return;
        }
        if (const auto *switch_stmt =
                dynamic_cast<const gen::SwitchStmt *>(&statement)) {
            SetLastNull();
            const ir::ValueRef condition = EmitExpr(switch_stmt->condition());
            const ir::BlockId end_block = builder_.CreateBlock("switch.end");

            break_targets_.push_back(end_block);

            ir::BlockId test_block = builder_.Current();
            for (const std::unique_ptr<gen::SwitchCase> &switch_case :
                 switch_stmt->cases()) {
                if (!switch_case) {
                    continue;
                }
                const ir::BlockId case_block =
                    builder_.CreateBlock("switch.case");
                const ir::BlockId next_test =
                    builder_.CreateBlock("switch.next");

                builder_.SetCurrent(test_block);
                const ir::ValueRef match = EmitExpr(switch_case->match());
                const ir::ValueRef cmp =
                    EmitBinary(ir::BinaryOp::Equal, condition, match);
                builder_.SetTerminator(
                    ir::BranchTerm{.condition = cmp,
                                   .true_target = case_block,
                                   .false_target = next_test});

                builder_.SetCurrent(case_block);
                EnterScope();
                EmitStatementList(switch_case->body());
                ExitScope();
                EmitJumpIfNotTerminated(end_block);

                test_block = next_test;
            }

            builder_.SetCurrent(test_block);
            EnterScope();
            EmitStatementList(switch_stmt->default_body());
            ExitScope();
            EmitJumpIfNotTerminated(end_block);

            break_targets_.pop_back();

            builder_.SetCurrent(end_block);
            return;
        }

        throw InterpreterException("unsupported statement node in IR lowering");
    }
};

ir::Function
CompileFunctionBody(std::string name, std::vector<std::string> params,
                    std::vector<std::string> param_types,
                    const std::vector<std::unique_ptr<gen::Statement>> &body,
                    bool is_method = false, std::string owning_class = {},
                    std::string method_name = {},
                    bool module_scope_globals = false) {
    ir::Function function;
    function.name = std::move(name);
    function.params = std::move(params);
    function.param_types = std::move(param_types);
    if (function.param_types.size() != function.params.size()) {
        throw InterpreterException("function '" + function.name +
                                   "' has mismatched parameter type metadata");
    }
    function.param_slots.reserve(function.params.size());
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        const std::string &param = function.params[i];
        if (is_method && (param == "self" || param == "this")) {
            throw InterpreterException("method '" + function.name +
                                       "' parameter names cannot use reserved identifier '" +
                                       param + "'");
        }
        const ir::SlotId slot = function.next_slot++;
        function.param_slots.push_back(slot);
        function.slot_names.push_back(param);
        function.slot_types.push_back(function.param_types[i]);
    }
    if (is_method) {
        function.slot_names.push_back("self");
        ++function.next_slot;
        function.slot_types.push_back(owning_class);
    }
    function.blocks.push_back(ir::BasicBlock{.id = 0, .label = "entry"});
    function.entry = 0;
    function.is_method = is_method;
    function.owning_class = std::move(owning_class);
    function.method_name = std::move(method_name);

    IRFunctionCodegen codegen(function, module_scope_globals);
    codegen.EmitStatementList(body);
    codegen.Finalize();
    return function;
}

std::vector<std::filesystem::path> ListStandardLibraryFiles() {
    std::vector<std::filesystem::path> out;
#ifdef INTERPRETER_STDLIB_DIR
    const std::filesystem::path stdlib_dir(INTERPRETER_STDLIB_DIR);
    if (!std::filesystem::exists(stdlib_dir)) {
        throw InterpreterException(
            "standard library directory does not exist: " +
            stdlib_dir.string());
    }
    if (!std::filesystem::is_directory(stdlib_dir)) {
        throw InterpreterException(
            "standard library path is not a directory: " + stdlib_dir.string());
    }

    const std::vector<std::string> ordered_files = {
        "Core.neon",    "IO.neon",        "Math.neon",
        "Complex.neon", "ArrayList.neon", "LinkedList.neon",
        "HashMap.neon", "Deflate.neon",   "PNG.neon",
    };

    std::unordered_set<std::string> known_names;
    known_names.reserve(ordered_files.size());
    for (const std::string &file_name : ordered_files) {
        const std::filesystem::path component_path = stdlib_dir / file_name;
        if (!std::filesystem::exists(component_path) ||
            !std::filesystem::is_regular_file(component_path)) {
            throw InterpreterException(
                "required standard library component is missing: " +
                component_path.string());
        }
        out.push_back(component_path);
        known_names.insert(file_name);
    }

    std::vector<std::filesystem::path> extra_files;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(stdlib_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string file_name = entry.path().filename().string();
        if (known_names.contains(file_name)) {
            continue;
        }
        extra_files.push_back(entry.path());
    }
    std::sort(
        extra_files.begin(), extra_files.end(),
        [](const std::filesystem::path &lhs, const std::filesystem::path &rhs) {
            return lhs.filename().string() < rhs.filename().string();
        });
    out.insert(out.end(), extra_files.begin(), extra_files.end());
#endif
    return out;
}

AST ParseProgramFile(const std::filesystem::path &source_path) {
    try {
        return ParseProgram(compiler::common::ReadTextFile(source_path));
    } catch (const std::exception &ex) {
        throw InterpreterException(
            "failed to load standard library component '" +
            source_path.string() + "': " + ex.what());
    }
}

} // namespace

ir::Program CompileProgramToIR(const AST &ast) {
    try {
        const gen::Program &program = RequireProgram(ast);

        ir::Program out;

        ir::Function main_fn;
        main_fn.name = "__main__";
        main_fn.blocks.push_back(ir::BasicBlock{.id = 0, .label = "entry"});
        main_fn.entry = 0;
        IRFunctionCodegen main_codegen(main_fn, true);

        for (const std::unique_ptr<gen::Item> &item : program.items().items()) {
            if (!item) {
                continue;
            }

            if (const auto *function =
                    dynamic_cast<const gen::FunctionDecl *>(item.get())) {
                out.functions.push_back(CompileFunctionBody(
                    function->name(), function->params(),
                    function->param_types(), function->body()));
                continue;
            }

            if (const auto *class_decl =
                    dynamic_cast<const gen::ClassDecl *>(item.get())) {
                ir::ClassInfo class_info;
                class_info.name = class_decl->name();

                std::unordered_set<std::string> seen_fields;
                std::unordered_set<std::string> seen_methods;
                bool seen_constructor = false;

                for (const std::unique_ptr<gen::ClassMember> &member :
                     class_decl->members()) {
                    if (!member) {
                        continue;
                    }

                    if (const auto *field =
                            dynamic_cast<const gen::FieldDecl *>(
                                member.get())) {
                        if (!seen_fields.insert(field->name()).second) {
                            throw InterpreterException(
                                "duplicate field '" + field->name() +
                                "' in class '" + class_decl->name() + "'");
                        }
                        const ir::SlotId field_offset =
                            static_cast<ir::SlotId>(class_info.fields.size());
                        class_info.fields.push_back(field->name());
                        class_info.field_types.push_back(field->type_name());
                        class_info.field_offsets[field->name()] = field_offset;
                        continue;
                    }

                    if (const auto *method =
                            dynamic_cast<const gen::MethodDecl *>(
                                member.get())) {
                        if (method->name() == class_decl->name()) {
                            throw InterpreterException(
                                "method '" + method->name() + "' in class '" +
                                class_decl->name() +
                                "' conflicts with constructor syntax; declare "
                                "constructors as '" +
                                class_decl->name() + "(...)'");
                        }
                        if (!seen_methods.insert(method->name()).second) {
                            throw InterpreterException(
                                "duplicate method '" + method->name() +
                                "' in class '" + class_decl->name() + "'");
                        }

                        const std::string function_name =
                            class_decl->name() + "." + method->name();
                        const ir::SlotId method_slot = static_cast<ir::SlotId>(
                            class_info.vtable_functions.size());
                        class_info.method_slots[method->name()] = method_slot;
                        class_info.vtable_functions.push_back(function_name);
                        class_info.method_functions[method->name()] =
                            function_name;

                        out.functions.push_back(CompileFunctionBody(
                            function_name, method->params(),
                            method->param_types(), method->body(), true,
                            class_decl->name(), method->name()));
                        continue;
                    }

                    if (const auto *ctor =
                            dynamic_cast<const gen::ConstructorDecl *>(
                                member.get())) {
                        if (ctor->declared_name() != class_decl->name()) {
                            throw InterpreterException(
                                "constructor '" + ctor->declared_name() +
                                "' does not match class name '" +
                                class_decl->name() + "'");
                        }
                        if (seen_constructor) {
                            throw InterpreterException(
                                "duplicate constructor in class '" +
                                class_decl->name() + "'");
                        }
                        seen_constructor = true;

                        const std::string function_name =
                            class_decl->name() + ".__ctor";
                        class_info.constructor_function = function_name;
                        out.functions.push_back(CompileFunctionBody(
                            function_name, ctor->params(), ctor->param_types(),
                            ctor->body(), true, class_decl->name(), "__ctor"));
                        continue;
                    }

                    throw InterpreterException(
                        "unsupported class member in class '" +
                        class_decl->name() + "'");
                }

                out.classes.push_back(std::move(class_info));
                continue;
            }

            if (const auto *top_stmt =
                    dynamic_cast<const gen::TopStatement *>(item.get())) {
                main_codegen.EmitTopLevelStatement(top_stmt->statement());
                continue;
            }
        }

        main_codegen.Finalize();
        out.functions.push_back(std::move(main_fn));
        out.entry_function = "__main__";

        return out;
    } catch (const ir::IRException &ex) {
        throw InterpreterException(ex.what());
    }
}

std::string ValueToString(const Value &value) {
    return ir::ValueToString(value);
}

std::vector<ir::ProgramUnit> CompileStandardLibraryToIR() {
    std::vector<ir::ProgramUnit> units;
    for (const std::filesystem::path &path : ListStandardLibraryFiles()) {
        const AST ast = ParseProgramFile(path);
        units.push_back(ir::ProgramUnit{.name = path.string(),
                                        .program = CompileProgramToIR(ast)});
    }
    return units;
}

Value ExecuteIRProgram(ir::Program program,
                       std::vector<ir::ProgramUnit> prelude_units,
                       bool optimize) {
    try {
        if (optimize) {
            for (ir::ProgramUnit &unit : prelude_units) {
                ir::OptimizeProgram(unit.program);
            }
            ir::OptimizeProgram(program);
        }
        return ir::ExecuteProgram(program, prelude_units);
    } catch (const ir::IRException &ex) {
        throw InterpreterException(ex.what());
    }
}

Value InterpretProgram(const AST &ast) {
    const ir::Program main_program = CompileProgramToIR(ast);
    std::vector<ir::ProgramUnit> stdlib_units = CompileStandardLibraryToIR();
    return ExecuteIRProgram(main_program, std::move(stdlib_units), true);
}

Value InterpretSource(std::string_view source_text) {
    const AST ast = ParseProgram(source_text);
    return InterpretProgram(ast);
}

} // namespace compiler::interpreter
