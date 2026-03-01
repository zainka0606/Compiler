#include "Frontend.h"

#include "Common/FileIO.h"
#include "CompilerPipeline.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace compiler::frontend {

namespace {

namespace gen = generated::Neon::ast;
using AST = frontend_pipeline::AST;

const gen::Program &RequireProgram(const AST &ast) {
    if (ast.Empty()) {
        throw FrontendException("program AST is empty");
    }
    const auto *program = dynamic_cast<const gen::Program *>(&ast.Root());
    if (program == nullptr) {
        throw FrontendException("program AST root is not Program");
    }
    return *program;
}

std::filesystem::path NormalizeSourcePath(const std::filesystem::path &path) {
    const std::filesystem::path absolute =
        path.is_absolute() ? path : std::filesystem::absolute(path);
    if (!std::filesystem::exists(absolute) ||
        !std::filesystem::is_regular_file(absolute)) {
        throw FrontendException("source file does not exist: " +
                                absolute.string());
    }
    return std::filesystem::weakly_canonical(absolute);
}

bool IsRegularFilePath(const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path CanonicalizeExistingPath(const std::filesystem::path &path) {
    const std::filesystem::path absolute =
        path.is_absolute() ? path : std::filesystem::absolute(path);
    return std::filesystem::weakly_canonical(absolute);
}

std::optional<std::string> ExtractDeclaredModule(const AST &ast) {
    const gen::Program &program = RequireProgram(ast);
    std::optional<std::string> module_name;
    for (const std::unique_ptr<gen::Item> &item : program.items().items()) {
        if (!item) {
            continue;
        }
        const auto *module_decl = dynamic_cast<const gen::ModuleDecl *>(item.get());
        if (module_decl == nullptr) {
            continue;
        }
        if (module_name.has_value()) {
            throw FrontendException("duplicate mod declaration in source unit");
        }
        module_name = module_decl->name();
    }
    return module_name;
}

std::vector<std::string> ExtractUseModules(const AST &ast) {
    const gen::Program &program = RequireProgram(ast);
    std::vector<std::string> out;
    for (const std::unique_ptr<gen::Item> &item : program.items().items()) {
        if (!item) {
            continue;
        }
        const auto *use_decl = dynamic_cast<const gen::UseDecl *>(item.get());
        if (use_decl == nullptr) {
            continue;
        }
        out.push_back(use_decl->name());
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::filesystem::path ResolveUsePath(const std::filesystem::path &from_file,
                                     const std::string_view module_name) {
    const std::filesystem::path local_candidate =
        from_file.parent_path() / (std::string(module_name) + ".neon");
    if (IsRegularFilePath(local_candidate)) {
        return CanonicalizeExistingPath(local_candidate);
    }

#ifdef FRONTEND_STDLIB_DIR
    const std::filesystem::path stdlib_candidate =
        std::filesystem::path(FRONTEND_STDLIB_DIR) /
        (std::string(module_name) + ".neon");
    if (IsRegularFilePath(stdlib_candidate)) {
        return CanonicalizeExistingPath(stdlib_candidate);
    }
#endif

    throw FrontendException("module '" + std::string(module_name) +
                            "' imported from '" + from_file.string() +
                            "' was not found");
}

struct ModuleRecord {
    std::filesystem::path path;
    std::string module_name;
    AST ast;
    bool is_entry = false;
};

class ModuleLoader {
  public:
    explicit ModuleLoader(std::filesystem::path entry_path)
        : entry_path_(std::move(entry_path)) {}

    void Load() { Visit(entry_path_, true); }

    [[nodiscard]] std::vector<ModuleRecord> OrderedModules() {
        std::vector<ModuleRecord> out;
        out.reserve(postorder_.size());
        for (const std::string &key : postorder_) {
            auto it = modules_.find(key);
            if (it == modules_.end()) {
                continue;
            }
            out.push_back(std::move(it->second));
        }
        return out;
    }

  private:
    std::filesystem::path entry_path_;
    std::unordered_map<std::string, ModuleRecord> modules_;
    std::unordered_map<std::string, std::uint8_t> visit_state_;
    std::unordered_map<std::string, std::string> module_name_to_path_key_;
    std::vector<std::string> postorder_;

    void Visit(const std::filesystem::path &path, const bool is_entry) {
        const std::filesystem::path normalized = NormalizeSourcePath(path);
        const std::string key = normalized.string();

        const std::uint8_t state = visit_state_[key];
        if (state == 2) {
            if (is_entry) {
                auto found = modules_.find(key);
                if (found != modules_.end()) {
                    found->second.is_entry = true;
                }
            }
            return;
        }
        if (state == 1) {
            throw FrontendException("cyclic module import detected at: " + key);
        }

        visit_state_[key] = 1;

        const std::string source_text = common::ReadTextFile(normalized);
        AST ast;
        try {
            ast = frontend_pipeline::ParseProgram(source_text);
        } catch (const frontend_pipeline::FrontendPipelineException &ex) {
            throw FrontendException(ex.what());
        }

        std::string module_name = normalized.stem().string();
        if (const std::optional<std::string> declared = ExtractDeclaredModule(ast);
            declared.has_value() && !declared->empty()) {
            module_name = *declared;
        }

        const auto existing_module = module_name_to_path_key_.find(module_name);
        if (existing_module != module_name_to_path_key_.end() &&
            existing_module->second != key) {
            throw FrontendException("module '" + module_name +
                                    "' declared by multiple files: " + key +
                                    " and " + existing_module->second);
        }
        module_name_to_path_key_[module_name] = key;

        const std::vector<std::string> uses = ExtractUseModules(ast);
        modules_[key] = ModuleRecord{
            .path = normalized,
            .module_name = module_name,
            .ast = std::move(ast),
            .is_entry = is_entry,
        };

        for (const std::string &use_name : uses) {
            Visit(ResolveUsePath(normalized, use_name), false);
        }

        visit_state_[key] = 2;
        postorder_.push_back(key);
    }
};

struct ModuleExports {
    std::unordered_set<std::string> functions;
    std::unordered_set<std::string> classes;
    std::unordered_set<std::string> direct_uses;
};

using OwnerMap = std::unordered_map<std::string, std::vector<std::string>>;

bool IsBuiltinFunctionName(const std::string_view name) {
    return name == "sin" || name == "cos" || name == "tan" ||
           name == "sqrt" || name == "abs" || name == "exp" ||
           name == "ln" || name == "log10" || name == "pow" ||
           name == "min" || name == "max" || name == "sum" ||
           name == "print" || name == "println" || name == "readln" ||
           name == "input" || name == "read_file" ||
           name == "write_file" || name == "append_file" ||
           name == "read_binary_file" || name == "write_binary_file" ||
           name == "append_binary_file" || name == "file_exists" ||
           name == "file_size" || name == "len" ||
           name == "__array_push" || name == "__array_pop" ||
           name == "__string_concat" || name == "__cast" ||
           name == "alloc" || name == "stack_alloc";
}

std::vector<std::string> UniqueSorted(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void ValidateVisibleName(
    const std::string_view module_name, const std::string_view name,
    const std::string_view kind,
    const std::unordered_set<std::string> &visible_modules,
    const OwnerMap &owners_by_name) {
    const auto found = owners_by_name.find(std::string(name));
    if (found == owners_by_name.end()) {
        return;
    }

    bool visible = false;
    for (const std::string &owner : found->second) {
        if (visible_modules.contains(owner)) {
            visible = true;
            break;
        }
    }
    if (visible) {
        return;
    }

    std::vector<std::string> owners = UniqueSorted(found->second);
    std::string required_uses;
    for (const std::string &owner : owners) {
        if (!required_uses.empty()) {
            required_uses += ", ";
        }
        required_uses += owner;
    }

    throw FrontendException("module '" + std::string(module_name) +
                            "' references " + std::string(kind) + " '" +
                            std::string(name) +
                            "' without importing its module; add use for: " +
                            required_uses);
}

void ValidateExprAccess(const gen::Expr &expr, std::string_view module_name,
                        const std::unordered_set<std::string> &visible_modules,
                        const OwnerMap &function_owners,
                        const OwnerMap &class_owners);

void ValidateStatementAccess(
    const gen::Statement &statement, const std::string_view module_name,
    const std::unordered_set<std::string> &visible_modules,
    const OwnerMap &function_owners, const OwnerMap &class_owners) {
    if (const auto *let_stmt = dynamic_cast<const gen::LetStmt *>(&statement)) {
        ValidateExprAccess(let_stmt->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *return_stmt =
            dynamic_cast<const gen::ReturnStmt *>(&statement)) {
        ValidateExprAccess(return_stmt->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *expr_stmt = dynamic_cast<const gen::ExprStmt *>(&statement)) {
        ValidateExprAccess(expr_stmt->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *if_stmt = dynamic_cast<const gen::IfStmt *>(&statement)) {
        ValidateExprAccess(if_stmt->condition(), module_name, visible_modules,
                           function_owners, class_owners);
        for (const std::unique_ptr<gen::Statement> &stmt : if_stmt->then_body()) {
            if (stmt) {
                ValidateStatementAccess(*stmt, module_name, visible_modules,
                                        function_owners, class_owners);
            }
        }
        for (const std::unique_ptr<gen::Statement> &stmt : if_stmt->else_body()) {
            if (stmt) {
                ValidateStatementAccess(*stmt, module_name, visible_modules,
                                        function_owners, class_owners);
            }
        }
        return;
    }
    if (const auto *while_stmt =
            dynamic_cast<const gen::WhileStmt *>(&statement)) {
        ValidateExprAccess(while_stmt->condition(), module_name, visible_modules,
                           function_owners, class_owners);
        for (const std::unique_ptr<gen::Statement> &stmt : while_stmt->body()) {
            if (stmt) {
                ValidateStatementAccess(*stmt, module_name, visible_modules,
                                        function_owners, class_owners);
            }
        }
        return;
    }
    if (const auto *for_stmt = dynamic_cast<const gen::ForStmt *>(&statement)) {
        ValidateExprAccess(for_stmt->init(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(for_stmt->condition(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(for_stmt->update(), module_name, visible_modules,
                           function_owners, class_owners);
        for (const std::unique_ptr<gen::Statement> &stmt : for_stmt->body()) {
            if (stmt) {
                ValidateStatementAccess(*stmt, module_name, visible_modules,
                                        function_owners, class_owners);
            }
        }
        return;
    }
    if (const auto *switch_stmt =
            dynamic_cast<const gen::SwitchStmt *>(&statement)) {
        ValidateExprAccess(switch_stmt->condition(), module_name, visible_modules,
                           function_owners, class_owners);
        for (const std::unique_ptr<gen::SwitchCase> &switch_case :
             switch_stmt->cases()) {
            if (!switch_case) {
                continue;
            }
            ValidateExprAccess(switch_case->match(), module_name, visible_modules,
                               function_owners, class_owners);
            for (const std::unique_ptr<gen::Statement> &stmt :
                 switch_case->body()) {
                if (stmt) {
                    ValidateStatementAccess(*stmt, module_name, visible_modules,
                                            function_owners, class_owners);
                }
            }
        }
        for (const std::unique_ptr<gen::Statement> &stmt :
             switch_stmt->default_body()) {
            if (stmt) {
                ValidateStatementAccess(*stmt, module_name, visible_modules,
                                        function_owners, class_owners);
            }
        }
        return;
    }
}

void ValidateExprAccess(const gen::Expr &expr, const std::string_view module_name,
                        const std::unordered_set<std::string> &visible_modules,
                        const OwnerMap &function_owners,
                        const OwnerMap &class_owners) {
    if (dynamic_cast<const gen::Number *>(&expr) != nullptr ||
        dynamic_cast<const gen::StringLiteral *>(&expr) != nullptr ||
        dynamic_cast<const gen::CharLiteral *>(&expr) != nullptr ||
        dynamic_cast<const gen::BoolLiteral *>(&expr) != nullptr ||
        dynamic_cast<const gen::Identifier *>(&expr) != nullptr) {
        return;
    }

    if (dynamic_cast<const gen::FormattedString *>(&expr) != nullptr) {
        ValidateVisibleName(module_name, "stringConcat", "function",
                            visible_modules, function_owners);
        return;
    }

    if (const auto *let_expr = dynamic_cast<const gen::LetExpr *>(&expr)) {
        ValidateExprAccess(let_expr->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *assign_expr = dynamic_cast<const gen::AssignExpr *>(&expr)) {
        ValidateExprAccess(assign_expr->target(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(assign_expr->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *compound_assign =
            dynamic_cast<const gen::CompoundAssignExpr *>(&expr)) {
        ValidateExprAccess(compound_assign->target(), module_name,
                           visible_modules, function_owners, class_owners);
        ValidateExprAccess(compound_assign->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *cast_expr = dynamic_cast<const gen::CastExpr *>(&expr)) {
        ValidateExprAccess(cast_expr->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *add = dynamic_cast<const gen::Add *>(&expr)) {
        ValidateExprAccess(add->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(add->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *sub = dynamic_cast<const gen::Subtract *>(&expr)) {
        ValidateExprAccess(sub->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(sub->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *mul = dynamic_cast<const gen::Multiply *>(&expr)) {
        ValidateExprAccess(mul->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(mul->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *div = dynamic_cast<const gen::Divide *>(&expr)) {
        ValidateExprAccess(div->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(div->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *idiv = dynamic_cast<const gen::IntDivide *>(&expr)) {
        ValidateExprAccess(idiv->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(idiv->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *mod = dynamic_cast<const gen::Modulo *>(&expr)) {
        ValidateExprAccess(mod->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(mod->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *pow_expr = dynamic_cast<const gen::Pow *>(&expr)) {
        ValidateExprAccess(pow_expr->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(pow_expr->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *neg = dynamic_cast<const gen::Negate *>(&expr)) {
        ValidateExprAccess(neg->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *logical_not = dynamic_cast<const gen::LogicalNot *>(&expr)) {
        ValidateExprAccess(logical_not->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *bitwise_not = dynamic_cast<const gen::BitwiseNot *>(&expr)) {
        ValidateExprAccess(bitwise_not->expr(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *logical_and =
            dynamic_cast<const gen::LogicalAnd *>(&expr)) {
        ValidateExprAccess(logical_and->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(logical_and->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *logical_or = dynamic_cast<const gen::LogicalOr *>(&expr)) {
        ValidateExprAccess(logical_or->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(logical_or->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *bit_and = dynamic_cast<const gen::BitwiseAnd *>(&expr)) {
        ValidateExprAccess(bit_and->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(bit_and->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *bit_or = dynamic_cast<const gen::BitwiseOr *>(&expr)) {
        ValidateExprAccess(bit_or->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(bit_or->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *bit_xor = dynamic_cast<const gen::BitwiseXor *>(&expr)) {
        ValidateExprAccess(bit_xor->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(bit_xor->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *shl = dynamic_cast<const gen::ShiftLeft *>(&expr)) {
        ValidateExprAccess(shl->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(shl->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *shr = dynamic_cast<const gen::ShiftRight *>(&expr)) {
        ValidateExprAccess(shr->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(shr->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *eq = dynamic_cast<const gen::Equal *>(&expr)) {
        ValidateExprAccess(eq->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(eq->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *neq = dynamic_cast<const gen::NotEqual *>(&expr)) {
        ValidateExprAccess(neq->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(neq->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *lt = dynamic_cast<const gen::Less *>(&expr)) {
        ValidateExprAccess(lt->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(lt->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *lte = dynamic_cast<const gen::LessEqual *>(&expr)) {
        ValidateExprAccess(lte->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(lte->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *gt = dynamic_cast<const gen::Greater *>(&expr)) {
        ValidateExprAccess(gt->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(gt->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }
    if (const auto *gte = dynamic_cast<const gen::GreaterEqual *>(&expr)) {
        ValidateExprAccess(gte->lhs(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(gte->rhs(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *ternary = dynamic_cast<const gen::Ternary *>(&expr)) {
        ValidateExprAccess(ternary->condition(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(ternary->when_true(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(ternary->when_false(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *call = dynamic_cast<const gen::Call *>(&expr)) {
        if (!IsBuiltinFunctionName(call->name())) {
            ValidateVisibleName(module_name, call->name(), "function",
                                visible_modules, function_owners);
        }
        for (const std::unique_ptr<gen::Expr> &arg : call->args()) {
            if (arg) {
                ValidateExprAccess(*arg, module_name, visible_modules,
                                   function_owners, class_owners);
            }
        }
        return;
    }

    if (const auto *new_expr = dynamic_cast<const gen::NewExpr *>(&expr)) {
        ValidateVisibleName(module_name, new_expr->name(), "class",
                            visible_modules, class_owners);
        for (const std::unique_ptr<gen::Expr> &arg : new_expr->args()) {
            if (arg) {
                ValidateExprAccess(*arg, module_name, visible_modules,
                                   function_owners, class_owners);
            }
        }
        return;
    }

    if (const auto *method_call = dynamic_cast<const gen::MethodCall *>(&expr)) {
        ValidateExprAccess(method_call->object(), module_name, visible_modules,
                           function_owners, class_owners);
        for (const std::unique_ptr<gen::Expr> &arg : method_call->args()) {
            if (arg) {
                ValidateExprAccess(*arg, module_name, visible_modules,
                                   function_owners, class_owners);
            }
        }
        return;
    }

    if (const auto *member = dynamic_cast<const gen::MemberAccess *>(&expr)) {
        ValidateExprAccess(member->object(), module_name, visible_modules,
                           function_owners, class_owners);
        return;
    }

    if (const auto *array_literal = dynamic_cast<const gen::ArrayLiteral *>(&expr)) {
        for (const std::unique_ptr<gen::Expr> &element : array_literal->elements()) {
            if (element) {
                ValidateExprAccess(*element, module_name, visible_modules,
                                   function_owners, class_owners);
            }
        }
        return;
    }

    if (const auto *index = dynamic_cast<const gen::IndexAccess *>(&expr)) {
        ValidateExprAccess(index->object(), module_name, visible_modules,
                           function_owners, class_owners);
        ValidateExprAccess(index->index(), module_name, visible_modules,
                           function_owners, class_owners);
    }
}

void ValidateModuleUseAccess(const std::vector<ModuleRecord> &modules,
                             const std::vector<ModuleRecord> &extra_modules) {
    std::unordered_map<std::string, ModuleExports> exports_by_module;
    exports_by_module.reserve(modules.size() + extra_modules.size());

    OwnerMap function_owners;
    OwnerMap class_owners;

    const auto collect_exports = [&](const ModuleRecord &module) {
        ModuleExports exports;
        for (const std::string &use_name : ExtractUseModules(module.ast)) {
            exports.direct_uses.insert(use_name);
        }

        const gen::Program &program = RequireProgram(module.ast);
        for (const std::unique_ptr<gen::Item> &item : program.items().items()) {
            if (!item) {
                continue;
            }
            if (const auto *function = dynamic_cast<const gen::FunctionDecl *>(item.get())) {
                exports.functions.insert(function->name());
                function_owners[function->name()].push_back(module.module_name);
                continue;
            }
            if (const auto *class_decl = dynamic_cast<const gen::ClassDecl *>(item.get())) {
                exports.classes.insert(class_decl->name());
                class_owners[class_decl->name()].push_back(module.module_name);
            }
        }

        auto [it, inserted] =
            exports_by_module.emplace(module.module_name, ModuleExports{});
        if (!inserted) {
            it->second.functions.insert(exports.functions.begin(),
                                        exports.functions.end());
            it->second.classes.insert(exports.classes.begin(),
                                      exports.classes.end());
            it->second.direct_uses.insert(exports.direct_uses.begin(),
                                          exports.direct_uses.end());
        } else {
            it->second = std::move(exports);
        }
    };

    for (const ModuleRecord &module : modules) {
        collect_exports(module);
    }
    for (const ModuleRecord &module : extra_modules) {
        collect_exports(module);
    }

    for (const ModuleRecord &module : modules) {
        const auto exports_it = exports_by_module.find(module.module_name);
        if (exports_it == exports_by_module.end()) {
            throw FrontendException("internal module export map error for module '" +
                                    module.module_name + "'");
        }

        std::unordered_set<std::string> visible_modules;
        visible_modules.insert(module.module_name);
        for (const std::string &use_name : exports_it->second.direct_uses) {
            if (!exports_by_module.contains(use_name)) {
                throw FrontendException("module '" + module.module_name +
                                        "' imports unknown module '" + use_name +
                                        "'");
            }
            visible_modules.insert(use_name);
        }

        const gen::Program &program = RequireProgram(module.ast);
        for (const std::unique_ptr<gen::Item> &item : program.items().items()) {
            if (!item) {
                continue;
            }

            if (const auto *function = dynamic_cast<const gen::FunctionDecl *>(item.get())) {
                for (const std::unique_ptr<gen::Statement> &statement : function->body()) {
                    if (statement) {
                        ValidateStatementAccess(*statement, module.module_name,
                                                visible_modules,
                                                function_owners, class_owners);
                    }
                }
                continue;
            }

            if (const auto *class_decl = dynamic_cast<const gen::ClassDecl *>(item.get())) {
                if (!class_decl->base_name().empty()) {
                    ValidateVisibleName(module.module_name, class_decl->base_name(),
                                        "class", visible_modules, class_owners);
                }
                for (const std::unique_ptr<gen::ClassMember> &member : class_decl->members()) {
                    if (!member) {
                        continue;
                    }
                    if (const auto *method = dynamic_cast<const gen::MethodDecl *>(member.get())) {
                        for (const std::unique_ptr<gen::Statement> &statement : method->body()) {
                            if (statement) {
                                ValidateStatementAccess(*statement,
                                                        module.module_name,
                                                        visible_modules,
                                                        function_owners,
                                                        class_owners);
                            }
                        }
                        continue;
                    }
                    if (const auto *ctor = dynamic_cast<const gen::ConstructorDecl *>(member.get())) {
                        for (const std::unique_ptr<gen::Statement> &statement : ctor->body()) {
                            if (statement) {
                                ValidateStatementAccess(*statement,
                                                        module.module_name,
                                                        visible_modules,
                                                        function_owners,
                                                        class_owners);
                            }
                        }
                    }
                }
                continue;
            }

            if (const auto *top_stmt = dynamic_cast<const gen::TopStatement *>(item.get())) {
                ValidateStatementAccess(top_stmt->statement(), module.module_name,
                                        visible_modules, function_owners,
                                        class_owners);
            }
        }
    }
}

std::vector<ModuleRecord> LoadStandardLibraryModuleIndex() {
    std::vector<ModuleRecord> out;
#ifdef FRONTEND_STDLIB_DIR
    const std::filesystem::path stdlib_dir(FRONTEND_STDLIB_DIR);
    if (!std::filesystem::exists(stdlib_dir) ||
        !std::filesystem::is_directory(stdlib_dir)) {
        throw FrontendException(
            "standard library directory is unavailable: " + stdlib_dir.string());
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(stdlib_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".neon") {
            continue;
        }
        files.push_back(NormalizeSourcePath(entry.path()));
    }
    std::sort(files.begin(), files.end(),
              [](const std::filesystem::path &lhs,
                 const std::filesystem::path &rhs) {
                  return lhs.filename().string() < rhs.filename().string();
              });

    out.reserve(files.size());
    for (const std::filesystem::path &path : files) {
        const std::string source_text = common::ReadTextFile(path);
        AST ast;
        try {
            ast = frontend_pipeline::ParseProgram(source_text);
        } catch (const frontend_pipeline::FrontendPipelineException &ex) {
            throw FrontendException("failed to parse standard library module '" +
                                    path.string() + "': " + ex.what());
        }

        std::string module_name = path.stem().string();
        if (const std::optional<std::string> declared = ExtractDeclaredModule(ast);
            declared.has_value() && !declared->empty()) {
            module_name = *declared;
        }

        out.push_back(ModuleRecord{
            .path = path,
            .module_name = std::move(module_name),
            .ast = std::move(ast),
            .is_entry = false,
        });
    }
#endif
    return out;
}

std::string TrimCopy(const std::string_view text) {
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
    return std::string(text.substr(begin, end - begin));
}

std::string BaseTypeName(const std::string_view type_name) {
    std::string current = TrimCopy(type_name);
    while (current.size() >= 2 &&
           current.substr(current.size() - 2) == "[]") {
        current = TrimCopy(
            std::string_view(current).substr(0, current.size() - 2));
    }
    return current;
}

bool IsBuiltinTypeName(const std::string_view type_name) {
    return type_name == "number" || type_name == "int" ||
           type_name == "bool" || type_name == "string" ||
           type_name == "char" || type_name == "array" ||
           type_name == "object" || type_name == "null" ||
           type_name == "any" || type_name.empty();
}

std::optional<std::string>
ClassNameFromAllocatorFunction(const std::string_view callee_name) {
    for (const std::string_view suffix : {".__new", ".__new_heap"}) {
        if (callee_name.size() <= suffix.size()) {
            continue;
        }
        if (callee_name.substr(callee_name.size() - suffix.size()) == suffix) {
            return std::string(
                callee_name.substr(0, callee_name.size() - suffix.size()));
        }
    }
    return std::nullopt;
}

void PruneUnusedFunctionsAndClasses(CompiledBundle &bundle) {
    struct FunctionRef {
        bool is_main = false;
        std::size_t unit_index = 0;
        std::size_t function_index = 0;
    };

    struct ClassRef {
        bool is_main = false;
        std::size_t unit_index = 0;
        std::size_t class_index = 0;
    };

    auto get_program_const = [&](const bool is_main,
                                 const std::size_t unit_index)
        -> const ir::Program & {
        return is_main ? bundle.program : bundle.prelude_units[unit_index].program;
    };

    std::unordered_map<std::string, std::vector<FunctionRef>> functions_by_name;
    std::unordered_map<std::string, std::vector<ClassRef>> classes_by_name;

    for (std::size_t i = 0; i < bundle.program.functions.size(); ++i) {
        const ir::Function &function = bundle.program.functions[i];
        functions_by_name[function.name].push_back(
            FunctionRef{.is_main = true, .unit_index = 0, .function_index = i});
    }
    for (std::size_t i = 0; i < bundle.program.classes.size(); ++i) {
        const ir::ClassInfo &class_info = bundle.program.classes[i];
        classes_by_name[class_info.name].push_back(
            ClassRef{.is_main = true, .unit_index = 0, .class_index = i});
    }

    for (std::size_t unit_index = 0; unit_index < bundle.prelude_units.size();
         ++unit_index) {
        const ir::Program &program = bundle.prelude_units[unit_index].program;
        for (std::size_t i = 0; i < program.functions.size(); ++i) {
            const ir::Function &function = program.functions[i];
            functions_by_name[function.name].push_back(
                FunctionRef{.is_main = false,
                            .unit_index = unit_index,
                            .function_index = i});
        }
        for (std::size_t i = 0; i < program.classes.size(); ++i) {
            const ir::ClassInfo &class_info = program.classes[i];
            classes_by_name[class_info.name].push_back(
                ClassRef{.is_main = false,
                         .unit_index = unit_index,
                         .class_index = i});
        }
    }

    std::unordered_set<std::string> live_function_names;
    std::unordered_set<std::string> live_class_names;
    std::queue<std::string> function_queue;
    std::queue<std::string> class_queue;

    const auto mark_function_name = [&](const std::string_view name) {
        if (!functions_by_name.contains(std::string(name))) {
            return;
        }
        if (live_function_names.insert(std::string(name)).second) {
            function_queue.push(std::string(name));
        }
    };

    const auto mark_class_name = [&](const std::string_view name) {
        if (!classes_by_name.contains(std::string(name))) {
            return;
        }
        if (live_class_names.insert(std::string(name)).second) {
            class_queue.push(std::string(name));
        }
    };

    const auto mark_type_name = [&](const std::string_view type_name) {
        const std::string base = BaseTypeName(type_name);
        if (IsBuiltinTypeName(base)) {
            return;
        }
        mark_class_name(base);
    };

    mark_function_name(bundle.program.entry_function);
    for (const ir::ProgramUnit &unit : bundle.prelude_units) {
        mark_function_name(unit.program.entry_function);
    }

    while (!function_queue.empty() || !class_queue.empty()) {
        while (!function_queue.empty()) {
            const std::string function_name = function_queue.front();
            function_queue.pop();

            const auto refs_it = functions_by_name.find(function_name);
            if (refs_it == functions_by_name.end()) {
                continue;
            }

            for (const FunctionRef &ref : refs_it->second) {
                const ir::Program &program = get_program_const(ref.is_main, ref.unit_index);
                if (ref.function_index >= program.functions.size()) {
                    continue;
                }
                const ir::Function &function = program.functions[ref.function_index];

                for (const std::string &type_name : function.param_types) {
                    mark_type_name(type_name);
                }
                for (const std::string &type_name : function.slot_types) {
                    mark_type_name(type_name);
                }

                for (const ir::BasicBlock &block : function.blocks) {
                    for (const ir::Instruction &instruction : block.instructions) {
                        std::visit(
                            [&](const auto &inst) {
                                using T = std::decay_t<decltype(inst)>;
                                if constexpr (std::is_same_v<T, ir::DeclareGlobalInst>) {
                                    mark_type_name(inst.type_name);
                                } else if constexpr (std::is_same_v<T, ir::CallInst>) {
                                    mark_function_name(inst.callee);
                                    if (const std::optional<std::string> class_name =
                                            ClassNameFromAllocatorFunction(
                                                inst.callee);
                                        class_name.has_value()) {
                                        mark_class_name(*class_name);
                                    }
                                    if ((inst.callee == "__new" ||
                                         inst.callee == "alloc" ||
                                         inst.callee == "stack_alloc") &&
                                        !inst.args.empty()) {
                                        const ir::ValueRef &type_arg = inst.args.front();
                                        if (type_arg.kind == ir::ValueKind::String) {
                                            mark_class_name(type_arg.text);
                                        }
                                    }
                                    if (inst.callee == "__cast" && inst.args.size() >= 2) {
                                        const ir::ValueRef &type_arg = inst.args[1];
                                        if (type_arg.kind == ir::ValueKind::String) {
                                            mark_class_name(type_arg.text);
                                        }
                                    }
                                }
                            },
                            instruction);
                    }
                }
            }
        }

        while (!class_queue.empty()) {
            const std::string class_name = class_queue.front();
            class_queue.pop();

            const auto refs_it = classes_by_name.find(class_name);
            if (refs_it == classes_by_name.end()) {
                continue;
            }

            for (const ClassRef &ref : refs_it->second) {
                const ir::Program &program = get_program_const(ref.is_main, ref.unit_index);
                if (ref.class_index >= program.classes.size()) {
                    continue;
                }
                const ir::ClassInfo &class_info = program.classes[ref.class_index];

                if (!class_info.base_class.empty()) {
                    mark_class_name(class_info.base_class);
                }
                for (const std::string &field_type : class_info.field_types) {
                    mark_type_name(field_type);
                }
                if (!class_info.constructor_function.empty()) {
                    mark_function_name(class_info.constructor_function);
                }
                for (const std::string &method_fn : class_info.vtable_functions) {
                    mark_function_name(method_fn);
                }
                for (const auto &[_, method_fn] : class_info.method_functions) {
                    mark_function_name(method_fn);
                }
            }
        }
    }

    const auto prune_program = [&](ir::Program &program,
                                   const std::string_view context_name) {
        program.functions.erase(
            std::remove_if(program.functions.begin(), program.functions.end(),
                           [&](const ir::Function &function) {
                               return !live_function_names.contains(function.name);
                           }),
            program.functions.end());

        program.classes.erase(
            std::remove_if(program.classes.begin(), program.classes.end(),
                           [&](const ir::ClassInfo &class_info) {
                               return !live_class_names.contains(class_info.name);
                           }),
            program.classes.end());

        const auto entry_it = std::find_if(
            program.functions.begin(), program.functions.end(),
            [&](const ir::Function &function) {
                return function.name == program.entry_function;
            });
        if (entry_it == program.functions.end()) {
            throw FrontendException("entry function '" + program.entry_function +
                                    "' was removed while pruning " +
                                    std::string(context_name));
        }
    };

    prune_program(bundle.program, "main program");
    for (ir::ProgramUnit &unit : bundle.prelude_units) {
        prune_program(unit.program, unit.name);
    }
}

} // namespace

CompiledBundle CompileEntryFile(const std::filesystem::path &entry_path,
                                const CompileOptions &options) {
    ModuleLoader loader(entry_path);
    loader.Load();
    std::vector<ModuleRecord> modules = loader.OrderedModules();
    if (modules.empty()) {
        throw FrontendException("no modules loaded from entry path: " +
                                entry_path.string());
    }

    const std::vector<ModuleRecord> stdlib_index =
        LoadStandardLibraryModuleIndex();
    ValidateModuleUseAccess(modules, stdlib_index);

    CompiledBundle bundle;
    try {
        bool saw_entry = false;
        for (ModuleRecord &module : modules) {
            ir::Program compiled = frontend_pipeline::CompileProgramToIR(module.ast);
            if (module.is_entry) {
                if (saw_entry) {
                    throw FrontendException(
                        "multiple entry modules detected during compilation");
                }
                bundle.program = std::move(compiled);
                saw_entry = true;
                continue;
            }

            bundle.prelude_units.push_back(ir::ProgramUnit{
                .name = module.path.string(),
                .program = std::move(compiled),
            });
        }

        if (!saw_entry) {
            throw FrontendException("entry module was not loaded");
        }
    } catch (const frontend_pipeline::FrontendPipelineException &ex) {
        throw FrontendException(ex.what());
    }

    PruneUnusedFunctionsAndClasses(bundle);

    if (options.optimize) {
        const ir::OptimizationOptions optimization_options{
            .disabled_passes = options.disabled_optimization_passes,
            .max_rounds = 8,
        };

        for (ir::ProgramUnit &unit : bundle.prelude_units) {
            ir::OptimizeProgram(unit.program, optimization_options);
        }
        ir::OptimizeProgram(bundle.program, optimization_options);

        PruneUnusedFunctionsAndClasses(bundle);
    }

    return bundle;
}

CompiledBundle CompileEntryFile(const std::filesystem::path &entry_path,
                                const bool optimize) {
    return CompileEntryFile(entry_path, CompileOptions{.optimize = optimize});
}

void WriteCompiledBundle(const CompiledBundle &bundle,
                         const std::filesystem::path &output_path) {
    ir::WriteProgramBundleBinary(
        ir::ProgramBundle{.program = bundle.program,
                          .prelude_units = bundle.prelude_units},
        output_path);
}

CompiledBundle ReadCompiledBundle(const std::filesystem::path &input_path) {
    const ir::ProgramBundle loaded = ir::ReadProgramBundleBinary(input_path);
    return CompiledBundle{.program = loaded.program,
                          .prelude_units = loaded.prelude_units};
}

} // namespace compiler::frontend
