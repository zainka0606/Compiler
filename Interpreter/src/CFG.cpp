#include "CFG.h"

#include "Common/Graphviz.h"
#include "Common/Identifier.h"
#include "Interpreter.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace compiler::interpreter {

namespace {

namespace gen = generated::MiniLangInterpreter::ast;

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

std::string DescribeExpr(const gen::Expr &expr) {
    if (const auto *number = dynamic_cast<const gen::Number *>(&expr)) {
        return "num(" + number->value() + ")";
    }
    if (const auto *formatted_string =
            dynamic_cast<const gen::FormattedString *>(&expr)) {
        return "fstr(" + formatted_string->value() + ")";
    }
    if (const auto *string_literal =
            dynamic_cast<const gen::StringLiteral *>(&expr)) {
        return "str(" + string_literal->value() + ")";
    }
    if (const auto *char_literal =
            dynamic_cast<const gen::CharLiteral *>(&expr)) {
        return "char(" + char_literal->value() + ")";
    }
    if (const auto *bool_literal =
            dynamic_cast<const gen::BoolLiteral *>(&expr)) {
        return "bool(" + bool_literal->value() + ")";
    }
    if (const auto *identifier = dynamic_cast<const gen::Identifier *>(&expr)) {
        return "id(" + identifier->name() + ")";
    }
    if (const auto *let_expr = dynamic_cast<const gen::LetExpr *>(&expr)) {
        return "let " + let_expr->name() + " = " + DescribeExpr(let_expr->expr());
    }
    if (const auto *assign_expr = dynamic_cast<const gen::AssignExpr *>(&expr)) {
        return "assign " + DescribeExpr(assign_expr->target()) + " = " +
               DescribeExpr(assign_expr->expr());
    }
    if (const auto *compound_assign_expr =
            dynamic_cast<const gen::CompoundAssignExpr *>(&expr)) {
        return "assign " + DescribeExpr(compound_assign_expr->target()) + " " +
               compound_assign_expr->op() + " " +
               DescribeExpr(compound_assign_expr->expr());
    }
    if (dynamic_cast<const gen::Add *>(&expr) != nullptr) {
        return "add";
    }
    if (dynamic_cast<const gen::Subtract *>(&expr) != nullptr) {
        return "sub";
    }
    if (dynamic_cast<const gen::Multiply *>(&expr) != nullptr) {
        return "mul";
    }
    if (dynamic_cast<const gen::Divide *>(&expr) != nullptr) {
        return "div";
    }
    if (dynamic_cast<const gen::IntDivide *>(&expr) != nullptr) {
        return "idiv";
    }
    if (dynamic_cast<const gen::Modulo *>(&expr) != nullptr) {
        return "mod";
    }
    if (dynamic_cast<const gen::Pow *>(&expr) != nullptr) {
        return "pow";
    }
    if (dynamic_cast<const gen::Negate *>(&expr) != nullptr) {
        return "neg";
    }
    if (dynamic_cast<const gen::LogicalNot *>(&expr) != nullptr) {
        return "not";
    }
    if (dynamic_cast<const gen::BitwiseNot *>(&expr) != nullptr) {
        return "bnot";
    }
    if (dynamic_cast<const gen::LogicalAnd *>(&expr) != nullptr) {
        return "land";
    }
    if (dynamic_cast<const gen::LogicalOr *>(&expr) != nullptr) {
        return "lor";
    }
    if (dynamic_cast<const gen::BitwiseAnd *>(&expr) != nullptr) {
        return "band";
    }
    if (dynamic_cast<const gen::BitwiseOr *>(&expr) != nullptr) {
        return "bor";
    }
    if (dynamic_cast<const gen::BitwiseXor *>(&expr) != nullptr) {
        return "bxor";
    }
    if (dynamic_cast<const gen::ShiftLeft *>(&expr) != nullptr) {
        return "shl";
    }
    if (dynamic_cast<const gen::ShiftRight *>(&expr) != nullptr) {
        return "shr";
    }
    if (dynamic_cast<const gen::Equal *>(&expr) != nullptr) {
        return "eq";
    }
    if (dynamic_cast<const gen::NotEqual *>(&expr) != nullptr) {
        return "neq";
    }
    if (dynamic_cast<const gen::Less *>(&expr) != nullptr) {
        return "lt";
    }
    if (dynamic_cast<const gen::LessEqual *>(&expr) != nullptr) {
        return "lte";
    }
    if (dynamic_cast<const gen::Greater *>(&expr) != nullptr) {
        return "gt";
    }
    if (dynamic_cast<const gen::GreaterEqual *>(&expr) != nullptr) {
        return "gte";
    }
    if (dynamic_cast<const gen::Ternary *>(&expr) != nullptr) {
        return "ternary";
    }
    if (const auto *call = dynamic_cast<const gen::Call *>(&expr)) {
        return "call(" + call->name() + ")";
    }
    if (const auto *array_literal =
            dynamic_cast<const gen::ArrayLiteral *>(&expr)) {
        return "array[" + std::to_string(array_literal->elements().size()) + "]";
    }
    if (const auto *method_call = dynamic_cast<const gen::MethodCall *>(&expr)) {
        return "call(" + DescribeExpr(method_call->object()) + "." + method_call->name() + ")";
    }
    if (const auto *index = dynamic_cast<const gen::IndexAccess *>(&expr)) {
        return "index(" + DescribeExpr(index->object()) + ")";
    }
    if (const auto *member = dynamic_cast<const gen::MemberAccess *>(&expr)) {
        return "member(" + DescribeExpr(member->object()) + "." + member->member() + ")";
    }
    return "expr";
}

std::string DescribeStatementNode(const gen::Statement &statement) {
    if (const auto *let_stmt = dynamic_cast<const gen::LetStmt *>(&statement)) {
        return "let " + let_stmt->name();
    }
    if (dynamic_cast<const gen::ReturnStmt *>(&statement) != nullptr) {
        return "return";
    }
    if (dynamic_cast<const gen::BreakStmt *>(&statement) != nullptr) {
        return "break";
    }
    if (const auto *expr_stmt =
            dynamic_cast<const gen::ExprStmt *>(&statement)) {
        return DescribeExpr(expr_stmt->expr());
    }
    if (const auto *if_stmt = dynamic_cast<const gen::IfStmt *>(&statement)) {
        return "if " + DescribeExpr(if_stmt->condition());
    }
    if (const auto *while_stmt =
            dynamic_cast<const gen::WhileStmt *>(&statement)) {
        return "while " + DescribeExpr(while_stmt->condition());
    }
    if (const auto *for_stmt = dynamic_cast<const gen::ForStmt *>(&statement)) {
        return "for " + DescribeExpr(for_stmt->condition());
    }
    if (const auto *switch_stmt = dynamic_cast<const gen::SwitchStmt *>(&statement)) {
        return "switch " + DescribeExpr(switch_stmt->condition());
    }
    return "stmt";
}

std::vector<const gen::Statement *> ToStatementPointers(
    const std::vector<std::unique_ptr<gen::Statement>> &statements) {
    std::vector<const gen::Statement *> out;
    out.reserve(statements.size());
    for (const std::unique_ptr<gen::Statement> &statement : statements) {
        if (statement != nullptr) {
            out.push_back(statement.get());
        }
    }
    return out;
}

class CFGBuilder {
  public:
    explicit CFGBuilder(std::string graph_name) {
        graph_.name = std::move(graph_name);
    }

    std::size_t AddNode(std::string label) {
        const std::size_t id = next_node_id_++;
        graph_.nodes.push_back(CFGNode{.id = id, .label = std::move(label)});
        return id;
    }

    void AddEdge(std::size_t from, std::size_t to, std::string label = {}) {
        graph_.edges.push_back(
            CFGEdge{.from = from, .to = to, .label = std::move(label)});
    }

    std::vector<std::size_t>
    BuildStatementList(const std::vector<const gen::Statement *> &statements,
                       std::vector<std::size_t> predecessors,
                       std::size_t function_exit_id) {
        for (const gen::Statement *statement : statements) {
            if (statement == nullptr) {
                continue;
            }
            predecessors = BuildStatement(*statement, std::move(predecessors),
                                          function_exit_id);
        }
        return predecessors;
    }

    CFGGraph BuildGraph(const std::vector<const gen::Statement *> &statements) {
        graph_.entry = AddNode("entry");
        graph_.exit = AddNode("exit");

        std::vector<std::size_t> fallthrough =
            BuildStatementList(statements, {graph_.entry}, graph_.exit);
        for (const std::size_t node_id : fallthrough) {
            AddEdge(node_id, graph_.exit);
        }
        return std::move(graph_);
    }

  private:
    std::vector<std::size_t>
    BuildStatement(const gen::Statement &statement,
                   std::vector<std::size_t> predecessors,
                   std::size_t function_exit_id) {
        const std::size_t statement_node =
            AddNode(DescribeStatementNode(statement));
        for (const std::size_t pred : predecessors) {
            AddEdge(pred, statement_node);
        }

        if (dynamic_cast<const gen::LetStmt *>(&statement) != nullptr ||
            dynamic_cast<const gen::ExprStmt *>(&statement) != nullptr) {
            return {statement_node};
        }

        if (dynamic_cast<const gen::ReturnStmt *>(&statement) != nullptr) {
            AddEdge(statement_node, function_exit_id, "return");
            return {};
        }
        if (dynamic_cast<const gen::BreakStmt *>(&statement) != nullptr) {
            if (!break_targets_.empty()) {
                AddEdge(statement_node, break_targets_.back(), "break");
            }
            return {};
        }

        if (const auto *if_stmt =
                dynamic_cast<const gen::IfStmt *>(&statement)) {
            std::vector<std::size_t> branch_exits;

            const std::size_t then_entry = AddNode("then");
            AddEdge(statement_node, then_entry, "T");
            std::vector<std::size_t> then_exits =
                BuildStatementList(ToStatementPointers(if_stmt->then_body()),
                                   {then_entry}, function_exit_id);
            branch_exits.insert(branch_exits.end(), then_exits.begin(),
                                then_exits.end());

            const std::size_t else_entry = AddNode("else");
            AddEdge(statement_node, else_entry, "F");
            if (if_stmt->else_body().empty()) {
                branch_exits.push_back(else_entry);
            } else {
                std::vector<std::size_t> else_exits = BuildStatementList(
                    ToStatementPointers(if_stmt->else_body()), {else_entry},
                    function_exit_id);
                branch_exits.insert(branch_exits.end(), else_exits.begin(),
                                    else_exits.end());
            }

            if (branch_exits.empty()) {
                return {};
            }
            if (branch_exits.size() == 1) {
                return branch_exits;
            }

            const std::size_t merge_node = AddNode("merge");
            for (const std::size_t branch_exit : branch_exits) {
                AddEdge(branch_exit, merge_node);
            }
            return {merge_node};
        }

        if (const auto *while_stmt =
                dynamic_cast<const gen::WhileStmt *>(&statement)) {
            const std::size_t while_end = AddNode("while_end");
            const std::size_t body_entry = AddNode("while_body");
            AddEdge(statement_node, body_entry, "T");
            break_targets_.push_back(while_end);
            const std::vector<std::size_t> body_exits = BuildStatementList(
                ToStatementPointers(while_stmt->body()), {body_entry},
                function_exit_id);
            break_targets_.pop_back();
            for (const std::size_t body_exit : body_exits) {
                AddEdge(body_exit, statement_node, "loop");
            }

            AddEdge(statement_node, while_end, "F");
            return {while_end};
        }

        if (const auto *for_stmt =
                dynamic_cast<const gen::ForStmt *>(&statement)) {
            const std::size_t init_node =
                AddNode("for_init " + DescribeExpr(for_stmt->init()));
            AddEdge(statement_node, init_node);

            const std::size_t cond_node =
                AddNode("for_cond " + DescribeExpr(for_stmt->condition()));
            AddEdge(init_node, cond_node);

            const std::size_t for_end = AddNode("for_end");
            const std::size_t body_entry = AddNode("for_body");
            AddEdge(cond_node, body_entry, "T");
            break_targets_.push_back(for_end);
            const std::vector<std::size_t> body_exits = BuildStatementList(
                ToStatementPointers(for_stmt->body()), {body_entry},
                function_exit_id);
            break_targets_.pop_back();

            if (!body_exits.empty()) {
                const std::size_t update_node = AddNode(
                    "for_update " + DescribeExpr(for_stmt->update()));
                for (const std::size_t body_exit : body_exits) {
                    AddEdge(body_exit, update_node);
                }
                AddEdge(update_node, cond_node, "loop");
            }

            AddEdge(cond_node, for_end, "F");
            return {for_end};
        }

        if (const auto *switch_stmt =
                dynamic_cast<const gen::SwitchStmt *>(&statement)) {
            const std::size_t switch_end = AddNode("switch_end");
            std::vector<std::size_t> exits;
            break_targets_.push_back(switch_end);
            for (std::size_t i = 0; i < switch_stmt->cases().size(); ++i) {
                const std::unique_ptr<gen::SwitchCase> &switch_case =
                    switch_stmt->cases()[i];
                if (!switch_case) {
                    continue;
                }
                const std::size_t case_entry =
                    AddNode("case " + DescribeExpr(switch_case->match()));
                AddEdge(statement_node, case_entry, "case");
                std::vector<std::size_t> case_exits =
                    BuildStatementList(ToStatementPointers(switch_case->body()),
                                       {case_entry}, function_exit_id);
                exits.insert(exits.end(), case_exits.begin(), case_exits.end());
            }

            if (!switch_stmt->default_body().empty()) {
                const std::size_t default_entry = AddNode("default");
                AddEdge(statement_node, default_entry, "default");
                std::vector<std::size_t> default_exits =
                    BuildStatementList(ToStatementPointers(switch_stmt->default_body()),
                                       {default_entry}, function_exit_id);
                exits.insert(exits.end(), default_exits.begin(), default_exits.end());
            }
            break_targets_.pop_back();

            for (const std::size_t exit : exits) {
                AddEdge(exit, switch_end);
            }
            if (switch_stmt->default_body().empty()) {
                AddEdge(statement_node, switch_end, "no-match");
            }
            return {switch_end};
        }

        return {statement_node};
    }

    CFGGraph graph_;
    std::size_t next_node_id_ = 0;
    std::vector<std::size_t> break_targets_;
};

CFGGraph
BuildGraphForStatements(std::string name,
                        const std::vector<const gen::Statement *> &statements) {
    CFGBuilder builder(std::move(name));
    return builder.BuildGraph(statements);
}

} // namespace

ProgramCFG BuildProgramCFG(const AST &ast) {
    const gen::Program &program = RequireProgram(ast);

    std::vector<const gen::Statement *> top_level_statements;
    std::vector<const gen::FunctionDecl *> functions;
    for (const std::unique_ptr<gen::Item> &item : program.items().items()) {
        if (item == nullptr) {
            continue;
        }
        if (const auto *function =
                dynamic_cast<const gen::FunctionDecl *>(item.get())) {
            functions.push_back(function);
            continue;
        }
        if (const auto *top_statement =
                dynamic_cast<const gen::TopStatement *>(item.get())) {
            top_level_statements.push_back(&top_statement->statement());
        }
    }

    ProgramCFG out;
    out.top_level = BuildGraphForStatements("top-level", top_level_statements);
    out.functions.reserve(functions.size());
    for (const gen::FunctionDecl *function : functions) {
        out.functions.push_back(BuildGraphForStatements(
            "fn " + function->name(), ToStatementPointers(function->body())));
    }
    return out;
}

std::string ProgramCFGToGraphvizDot(const ProgramCFG &cfg,
                                    std::string_view graph_name) {
    std::ostringstream out;
    out << "digraph "
        << compiler::common::SanitizeIdentifier(graph_name, "program_cfg")
        << " {\n";
    out << "  rankdir=TB;\n";
    out << "  node [shape=box];\n";

    std::size_t cluster_index = 0;
    const auto emit_graph = [&](const CFGGraph &graph) {
        out << "  subgraph cluster_" << cluster_index << " {\n";
        out << "    label=\""
            << compiler::common::EscapeGraphvizLabel(graph.name) << "\";\n";
        out << "    color=gray60;\n";
        for (const CFGNode &node : graph.nodes) {
            out << "    g" << cluster_index << "_n" << node.id << " [label=\""
                << compiler::common::EscapeGraphvizLabel(node.label) << "\"";
            if (node.id == graph.entry || node.id == graph.exit) {
                out << ", shape=oval";
            }
            out << "];\n";
        }
        for (const CFGEdge &edge : graph.edges) {
            out << "    g" << cluster_index << "_n" << edge.from << " -> g"
                << cluster_index << "_n" << edge.to;
            if (!edge.label.empty()) {
                out << " [label=\""
                    << compiler::common::EscapeGraphvizLabel(edge.label)
                    << "\"]";
            }
            out << ";\n";
        }
        out << "  }\n";
        ++cluster_index;
    };

    emit_graph(cfg.top_level);
    for (const CFGGraph &function : cfg.functions) {
        emit_graph(function);
    }

    out << "}\n";
    return out.str();
}

} // namespace compiler::interpreter
