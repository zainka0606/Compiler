#include "Bytecode.h"
#include "Common/FileIO.h"
#include "Common/Graphviz.h"
#include "CompilerPipeline.h"
#include "Frontend.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CLIOptions {
    bool optimize = true;
    bool dump_asm_to_stdout = false;
    bool list_optimization_passes = false;
    std::vector<std::string> disabled_optimization_passes;
    std::optional<std::filesystem::path> ast_dot_path;
    std::optional<std::filesystem::path> ir_dot_path;
    std::optional<std::filesystem::path> bytecode_dot_path;
    std::optional<std::filesystem::path> ir_asm_path;
    std::optional<std::filesystem::path> bytecode_asm_path;
    std::optional<std::filesystem::path> grammar_graph_dir;
    std::optional<std::filesystem::path> debug_dir;
};

class CLIException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

void PrintUsage() {
    std::cout
        << "usage: neonc <input.neon> <output.bin> [options]\n"
        << "options:\n"
        << "  --no-opt                             disable IR optimization\n"
        << "  --dump-asm                           print bytecode assembly to stdout\n"
        << "  --emit-ast-dot <file.dot>            write entry-source AST graph\n"
        << "  --emit-ir-dot <file.dot>             write IR CFG graph\n"
        << "  --emit-bytecode-dot <file.dot>       write bytecode CFG graph\n"
        << "  --emit-ir-asm <file.s>               write IR assembly\n"
        << "  --emit-bytecode-asm <file.s>         write bytecode assembly\n"
        << "  --emit-grammar-graphs <dir>          copy parser grammar graphs (AST/collection/table)\n"
        << "  --debug-dir <dir>                    write all debug outputs into dir\n"
        << "  --list-opts                          list optimization pass names\n"
        << "  --disable-opts=a,b,c                disable selected optimization passes\n";
}

[[nodiscard]] std::string ReadNextValue(const int argc, char **argv,
                                        int &index,
                                        const std::string_view option_name) {
    if (index + 1 >= argc) {
        throw CLIException("missing value for option " +
                           std::string(option_name));
    }
    ++index;
    return argv[index];
}

std::string TrimASCII(const std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

void ParseDisabledPassList(const std::string_view csv,
                           std::vector<std::string> &out) {
    std::size_t start = 0;
    while (start <= csv.size()) {
        const std::size_t comma = csv.find(',', start);
        const std::size_t end =
            (comma == std::string_view::npos) ? csv.size() : comma;
        const std::string token = TrimASCII(csv.substr(start, end - start));
        if (!token.empty()) {
            out.push_back(token);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void PrintOptimizationPassList() {
    std::cout << "Optimization passes:\n";
    for (const std::string &name : compiler::ir::ListOptimizationPasses()) {
        std::cout << "  " << name << "\n";
    }
}

CLIOptions ParseOptions(const int argc, char **argv) {
    CLIOptions options;
    for (int i = 3; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--no-opt") {
            options.optimize = false;
            continue;
        }
        if (flag == "--dump-asm") {
            options.dump_asm_to_stdout = true;
            continue;
        }
        if (flag == "--list-opts") {
            options.list_optimization_passes = true;
            continue;
        }
        if (flag.rfind("--disable-opts=", 0) == 0) {
            ParseDisabledPassList(flag.substr(std::string("--disable-opts=").size()),
                                  options.disabled_optimization_passes);
            continue;
        }
        if (flag == "--disable-opts") {
            const std::string value = ReadNextValue(argc, argv, i, flag);
            ParseDisabledPassList(value, options.disabled_optimization_passes);
            continue;
        }
        if (flag == "--emit-ast-dot") {
            options.ast_dot_path = ReadNextValue(argc, argv, i, flag);
            continue;
        }
        if (flag == "--emit-ir-dot") {
            options.ir_dot_path = ReadNextValue(argc, argv, i, flag);
            continue;
        }
        if (flag == "--emit-bytecode-dot") {
            options.bytecode_dot_path = ReadNextValue(argc, argv, i, flag);
            continue;
        }
        if (flag == "--emit-ir-asm") {
            options.ir_asm_path = ReadNextValue(argc, argv, i, flag);
            continue;
        }
        if (flag == "--emit-bytecode-asm") {
            options.bytecode_asm_path = ReadNextValue(argc, argv, i, flag);
            continue;
        }
        if (flag == "--emit-grammar-graphs") {
            options.grammar_graph_dir = ReadNextValue(argc, argv, i, flag);
            continue;
        }
        if (flag == "--debug-dir") {
            options.debug_dir = ReadNextValue(argc, argv, i, flag);
            continue;
        }

        throw CLIException("unknown option: " + flag);
    }

    if (options.debug_dir.has_value()) {
        const std::filesystem::path &dir = *options.debug_dir;
        if (!options.ast_dot_path.has_value()) {
            options.ast_dot_path = dir / "AST.dot";
        }
        if (!options.ir_dot_path.has_value()) {
            options.ir_dot_path = dir / "IR.dot";
        }
        if (!options.bytecode_dot_path.has_value()) {
            options.bytecode_dot_path = dir / "Bytecode.dot";
        }
        if (!options.ir_asm_path.has_value()) {
            options.ir_asm_path = dir / "IR.s";
        }
        if (!options.bytecode_asm_path.has_value()) {
            options.bytecode_asm_path = dir / "Bytecode.s";
        }
        if (!options.grammar_graph_dir.has_value()) {
            options.grammar_graph_dir = dir / "Grammar";
        }
    }

    return options;
}

void WriteTextOutput(const std::optional<std::filesystem::path> &path,
                     const std::string &text) {
    if (!path.has_value()) {
        return;
    }
    if (path->has_parent_path()) {
        std::filesystem::create_directories(path->parent_path());
    }
    compiler::common::WriteTextFile(*path, text);
}

std::string BuildIRAssemblyDump(const compiler::frontend::CompiledBundle &bundle,
                                std::string_view unit_name) {
    std::ostringstream out;
    for (const compiler::ir::ProgramUnit &unit : bundle.prelude_units) {
        out << compiler::ir::ProgramToAssembly(unit.program, unit.name);
        if (!unit.name.empty() && unit.name.back() != '\n') {
            out << "\n";
        }
    }
    out << compiler::ir::ProgramToAssembly(bundle.program, unit_name);
    return out.str();
}

std::string
BuildBytecodeAssemblyDump(const compiler::bytecode::ProgramBundle &bundle,
                          std::string_view unit_name) {
    return compiler::bytecode::ProgramBundleToAssembly(bundle, unit_name);
}

std::string BuildIRCFGDot(const compiler::frontend::CompiledBundle &bundle) {
    std::ostringstream out;
    out << "digraph ir_cfg {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box];\n";

    std::size_t next_cluster = 0;
    std::size_t next_node = 0;

    auto append_program = [&](const compiler::ir::Program &program,
                              const std::string_view unit_name) {
        const std::size_t unit_cluster = next_cluster++;
        out << "  subgraph cluster_" << unit_cluster << " {\n";
        out << "    label=\""
            << compiler::common::EscapeGraphvizLabel(
                   std::string("IR Unit: ") + std::string(unit_name))
            << "\";\n";

        for (std::size_t f = 0; f < program.functions.size(); ++f) {
            const compiler::ir::Function &function = program.functions[f];
            const std::size_t function_cluster = next_cluster++;
            out << "    subgraph cluster_" << function_cluster << " {\n";
            out << "      label=\""
                << compiler::common::EscapeGraphvizLabel(
                       std::string("func ") + function.name)
                << "\";\n";

            std::vector<std::string> block_nodes(function.blocks.size());
            for (std::size_t b = 0; b < function.blocks.size(); ++b) {
                const compiler::ir::BasicBlock &block = function.blocks[b];
                const std::string node_name = "n" + std::to_string(next_node++);
                block_nodes[b] = node_name;

                std::string label = "b" + std::to_string(block.id);
                if (!block.label.empty()) {
                    label += "\\n" + block.label;
                }
                label += "\\ninst=" +
                         std::to_string(block.instructions.size());
                out << "      " << node_name << " [label=\""
                    << compiler::common::EscapeGraphvizLabel(label) << "\"";
                if (block.id == function.entry) {
                    out << " [peripheries=2]";
                }
                out << ";\n";
            }

            const std::string ret_node = "n" + std::to_string(next_node++);
            out << "      " << ret_node << " [shape=oval,label=\"ret\"];\n";

            for (std::size_t b = 0; b < function.blocks.size(); ++b) {
                const compiler::ir::BasicBlock &block = function.blocks[b];
                if (!block.terminator.has_value()) {
                    continue;
                }

                const compiler::ir::Terminator &term = *block.terminator;
                if (const auto *jump = std::get_if<compiler::ir::JumpTerm>(&term)) {
                    if (jump->target < block_nodes.size()) {
                        out << "      " << block_nodes[b] << " -> "
                            << block_nodes[jump->target]
                            << " [label=\"jmp\"];\n";
                    }
                    continue;
                }
                if (const auto *branch =
                        std::get_if<compiler::ir::BranchTerm>(&term)) {
                    if (branch->true_target < block_nodes.size()) {
                        out << "      " << block_nodes[b] << " -> "
                            << block_nodes[branch->true_target]
                            << " [label=\"T\"];\n";
                    }
                    if (branch->false_target < block_nodes.size()) {
                        out << "      " << block_nodes[b] << " -> "
                            << block_nodes[branch->false_target]
                            << " [label=\"F\"];\n";
                    }
                    continue;
                }
                if (std::holds_alternative<compiler::ir::ReturnTerm>(term)) {
                    out << "      " << block_nodes[b] << " -> " << ret_node
                        << " [label=\"ret\"];\n";
                }
            }

            out << "    }\n";
        }

        out << "  }\n";
    };

    for (const compiler::ir::ProgramUnit &unit : bundle.prelude_units) {
        append_program(unit.program, unit.name);
    }
    append_program(bundle.program, "<program>");

    out << "}\n";
    return out.str();
}

const char *BytecodeOpcodeName(const compiler::bytecode::OpCode opcode) {
    using OpCode = compiler::bytecode::OpCode;
    switch (opcode) {
    case OpCode::Nop:
        return "nop";
    case OpCode::Load:
        return "ld";
    case OpCode::Store:
        return "st";
    case OpCode::Push:
        return "push";
    case OpCode::Pop:
        return "pop";
    case OpCode::DeclareGlobal:
        return "defg";
    case OpCode::Move:
        return "mov";
    case OpCode::Unary:
        return "unary";
    case OpCode::Binary:
        return "binary";
    case OpCode::Compare:
        return "cmp";
    case OpCode::MakeArray:
        return "array";
    case OpCode::StackAllocObject:
        return "salloc";
    case OpCode::Call:
        return "call";
    case OpCode::CallRegister:
        return "callr";
    case OpCode::Jump:
        return "jmp";
    case OpCode::JumpIfFalse:
        return "jif";
    case OpCode::JumpCarry:
        return "jc";
    case OpCode::JumpNotCarry:
        return "jnc";
    case OpCode::JumpZero:
        return "jz";
    case OpCode::JumpNotZero:
        return "jnz";
    case OpCode::JumpSign:
        return "js";
    case OpCode::JumpNotSign:
        return "jns";
    case OpCode::JumpOverflow:
        return "jo";
    case OpCode::JumpNotOverflow:
        return "jno";
    case OpCode::JumpAbove:
        return "ja";
    case OpCode::JumpAboveEqual:
        return "jae";
    case OpCode::JumpBelow:
        return "jb";
    case OpCode::JumpBelowEqual:
        return "jbe";
    case OpCode::JumpGreater:
        return "jg";
    case OpCode::JumpGreaterEqual:
        return "jge";
    case OpCode::JumpLess:
        return "jl";
    case OpCode::JumpLessEqual:
        return "jle";
    case OpCode::Return:
        return "ret";
    }
    return "?";
}

std::string
BuildBytecodeCFGDot(const compiler::bytecode::ProgramBundle &bundle) {
    std::ostringstream out;
    out << "digraph bytecode_cfg {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box];\n";

    std::size_t next_cluster = 0;
    std::size_t next_node = 0;

    auto append_program = [&](const compiler::bytecode::Program &program,
                              const std::string_view unit_name) {
        const std::size_t unit_cluster = next_cluster++;
        out << "  subgraph cluster_" << unit_cluster << " {\n";
        out << "    label=\""
            << compiler::common::EscapeGraphvizLabel(
                   std::string("Bytecode Unit: ") + std::string(unit_name))
            << "\";\n";

        for (const compiler::bytecode::Function &function : program.functions) {
            const std::size_t function_cluster = next_cluster++;
            out << "    subgraph cluster_" << function_cluster << " {\n";
            out << "      label=\""
                << compiler::common::EscapeGraphvizLabel(
                       std::string("func ") + function.name)
                << "\";\n";

            std::vector<std::string> nodes(function.code.size());
            for (std::size_t pc = 0; pc < function.code.size(); ++pc) {
                const std::string node_name = "n" + std::to_string(next_node++);
                nodes[pc] = node_name;

                const auto opcode = function.code[pc].opcode;
                const std::string label = "@" + std::to_string(pc) + " " +
                                          BytecodeOpcodeName(opcode);
                out << "      " << node_name << " [label=\""
                    << compiler::common::EscapeGraphvizLabel(label) << "\"";
                if (pc == function.entry_pc) {
                    out << " [peripheries=2]";
                }
                out << ";\n";
            }

            for (std::size_t pc = 0; pc < function.code.size(); ++pc) {
                const compiler::bytecode::Instruction &inst = function.code[pc];
                if (inst.opcode == compiler::bytecode::OpCode::Return) {
                    continue;
                }
                if (inst.opcode == compiler::bytecode::OpCode::Jump) {
                    if (inst.target < nodes.size()) {
                        out << "      " << nodes[pc] << " -> "
                            << nodes[inst.target]
                            << " [label=\"jmp\"];\n";
                    }
                    continue;
                }
                if (inst.opcode == compiler::bytecode::OpCode::JumpIfFalse ||
                    inst.opcode == compiler::bytecode::OpCode::JumpCarry ||
                    inst.opcode == compiler::bytecode::OpCode::JumpNotCarry ||
                    inst.opcode == compiler::bytecode::OpCode::JumpZero ||
                    inst.opcode == compiler::bytecode::OpCode::JumpNotZero ||
                    inst.opcode == compiler::bytecode::OpCode::JumpSign ||
                    inst.opcode == compiler::bytecode::OpCode::JumpNotSign ||
                    inst.opcode == compiler::bytecode::OpCode::JumpOverflow ||
                    inst.opcode == compiler::bytecode::OpCode::JumpNotOverflow ||
                    inst.opcode == compiler::bytecode::OpCode::JumpAbove ||
                    inst.opcode == compiler::bytecode::OpCode::JumpAboveEqual ||
                    inst.opcode == compiler::bytecode::OpCode::JumpBelow ||
                    inst.opcode == compiler::bytecode::OpCode::JumpBelowEqual ||
                    inst.opcode == compiler::bytecode::OpCode::JumpGreater ||
                    inst.opcode == compiler::bytecode::OpCode::JumpGreaterEqual ||
                    inst.opcode == compiler::bytecode::OpCode::JumpLess ||
                    inst.opcode == compiler::bytecode::OpCode::JumpLessEqual) {
                    if (inst.target < nodes.size()) {
                        out << "      " << nodes[pc] << " -> "
                            << nodes[inst.target]
                            << " [label=\"F\"];\n";
                    }
                    if (pc + 1 < nodes.size()) {
                        out << "      " << nodes[pc] << " -> "
                            << nodes[pc + 1] << " [label=\"T\"];\n";
                    }
                    continue;
                }

                if (pc + 1 < nodes.size()) {
                    out << "      " << nodes[pc] << " -> " << nodes[pc + 1]
                        << ";\n";
                }
            }

            out << "    }\n";
        }

        out << "  }\n";
    };

    for (const compiler::bytecode::ProgramUnit &unit : bundle.prelude_units) {
        append_program(unit.program, unit.name);
    }
    append_program(bundle.program, "<program>");

    out << "}\n";
    return out.str();
}

void CopyGrammarGraph(const std::filesystem::path &source,
                      const std::filesystem::path &destination) {
    if (!std::filesystem::exists(source) || !std::filesystem::is_regular_file(source)) {
        std::cerr << "warning: grammar graph source is missing: "
                  << source.string() << "\n";
        return;
    }
    if (destination.has_parent_path()) {
        std::filesystem::create_directories(destination.parent_path());
    }
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing);
}

void WriteGrammarGraphs(
    const std::optional<std::filesystem::path> &graph_dir_opt) {
    if (!graph_dir_opt.has_value()) {
        return;
    }

    const std::filesystem::path &graph_dir = *graph_dir_opt;
    std::filesystem::create_directories(graph_dir);

#ifdef FRONTEND_GRAMMAR_AST_DOT_PATH
    CopyGrammarGraph(FRONTEND_GRAMMAR_AST_DOT_PATH,
                     graph_dir / "GrammarAST.dot");
#endif
#ifdef FRONTEND_GRAMMAR_COLLECTION_DOT_PATH
    CopyGrammarGraph(FRONTEND_GRAMMAR_COLLECTION_DOT_PATH,
                     graph_dir / "CanonicalCollection.dot");
#endif
#ifdef FRONTEND_GRAMMAR_TABLE_DOT_PATH
    CopyGrammarGraph(FRONTEND_GRAMMAR_TABLE_DOT_PATH,
                     graph_dir / "ParseTable.dot");
#endif
}

} // namespace

int main(const int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--list-opts") {
        PrintOptimizationPassList();
        return 0;
    }

    if (argc < 3) {
        PrintUsage();
        return 2;
    }

    const std::filesystem::path input_path = argv[1];
    const std::filesystem::path output_path = argv[2];

    try {
        const CLIOptions options = ParseOptions(argc, argv);
        if (options.list_optimization_passes) {
            PrintOptimizationPassList();
        }

        if (options.ast_dot_path.has_value()) {
            const std::string source_text = compiler::common::ReadTextFile(input_path);
            const compiler::frontend_pipeline::AST ast =
                compiler::frontend_pipeline::ParseProgram(source_text);
            WriteTextOutput(options.ast_dot_path,
                            compiler::frontend_pipeline::ASTToGraphvizDot(ast));
        }

        const compiler::frontend::CompileOptions compile_options{
            .optimize = options.optimize,
            .disabled_optimization_passes =
                options.disabled_optimization_passes,
        };
        const compiler::frontend::CompiledBundle ir_bundle =
            compiler::frontend::CompileEntryFile(input_path, compile_options);

        WriteTextOutput(options.ir_asm_path,
                        BuildIRAssemblyDump(ir_bundle, input_path.string()));
        WriteTextOutput(options.ir_dot_path, BuildIRCFGDot(ir_bundle));

        const compiler::bytecode::ProgramBundle bytecode_bundle =
            compiler::bytecode::LowerIRBundle(compiler::ir::ProgramBundle{
                .program = ir_bundle.program,
                .prelude_units = ir_bundle.prelude_units});

        WriteTextOutput(options.bytecode_asm_path,
                        BuildBytecodeAssemblyDump(bytecode_bundle,
                                                  input_path.string()));
        WriteTextOutput(options.bytecode_dot_path,
                        BuildBytecodeCFGDot(bytecode_bundle));

        compiler::bytecode::WriteProgramBundleBinary(bytecode_bundle, output_path);

        if (options.dump_asm_to_stdout) {
            std::cout << BuildBytecodeAssemblyDump(bytecode_bundle, "<program>")
                      << "\n";
        }

        WriteGrammarGraphs(options.grammar_graph_dir);

        std::cout << "Compiled " << input_path.string() << " -> "
                  << output_path.string() << "\n";
        std::cout << "Prelude units: " << bytecode_bundle.prelude_units.size()
                  << "\n";
        return 0;
    } catch (const CLIException &ex) {
        std::cerr << "cli error: " << ex.what() << "\n";
    } catch (const compiler::frontend_pipeline::FrontendPipelineException &ex) {
        std::cerr << "frontend parse error: " << ex.what() << "\n";
    } catch (const compiler::frontend::FrontendException &ex) {
        std::cerr << "frontend error: " << ex.what() << "\n";
    } catch (const compiler::bytecode::BytecodeException &ex) {
        std::cerr << "bytecode error: " << ex.what() << "\n";
    } catch (const compiler::ir::IRException &ex) {
        std::cerr << "ir error: " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}
