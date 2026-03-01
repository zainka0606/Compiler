#include "Common/FileIO.h"
#include "Interpreter.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> Sorted(std::unordered_set<std::string> values) {
    std::vector<std::string> out(values.begin(), values.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> SortedKeys(
    const std::unordered_map<std::string, std::vector<std::string>> &values) {
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const auto &[key, _] : values) {
        out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: InterpreterAstTest <input> [ast-dot-output]\n";
        return 2;
    }

    try {
        const std::filesystem::path input_path = argv[1];
        const std::filesystem::path output_path =
            (argc == 3) ? std::filesystem::path(argv[2]) : "AST.dot";
        const std::filesystem::path cfg_output_path =
            output_path.parent_path() /
            (output_path.stem().string() + ".cfg.dot");

        const std::string source = compiler::common::ReadTextFile(input_path);
        const compiler::interpreter::AST ast =
            compiler::interpreter::ParseProgram(source);
        const compiler::ir::Program ir_program =
            compiler::interpreter::CompileProgramToIR(ast);
        compiler::ir::Program optimized_ir = ir_program;
        compiler::ir::OptimizeProgram(optimized_ir);
        const compiler::interpreter::ProgramCFG cfg =
            compiler::interpreter::BuildProgramCFG(ast);
        compiler::common::WriteTextFile(
            output_path, compiler::interpreter::ASTToGraphvizDot(ast));
        compiler::common::WriteTextFile(
            cfg_output_path,
            compiler::interpreter::ProgramCFGToGraphvizDot(cfg));
        const std::filesystem::path ir_output_path =
            output_path.parent_path() / (output_path.stem().string() + ".ir.s");
        const std::filesystem::path ir_opt_output_path =
            output_path.parent_path() /
            (output_path.stem().string() + ".opt.ir.s");
        compiler::common::WriteTextFile(
            ir_output_path,
            compiler::ir::ProgramToAssembly(ir_program, input_path.string()));
        compiler::common::WriteTextFile(
            ir_opt_output_path,
            compiler::ir::ProgramToAssembly(optimized_ir, input_path.string()));

        const compiler::interpreter::ProgramAnnotation annotation =
            compiler::interpreter::AnnotateProgram(ast);
        const compiler::interpreter::Value result =
            compiler::interpreter::InterpretProgram(ast);

        std::cout << "Wrote AST DOT to " << output_path.string() << "\n";
        std::cout << "Wrote CFG DOT to " << cfg_output_path.string() << "\n";
        std::cout << "Wrote IR ASM to " << ir_output_path.string() << "\n";
        std::cout << "Wrote optimized IR ASM to " << ir_opt_output_path.string()
                  << "\n";
        std::cout << "Flattened items:\n";
        for (const std::string &line : annotation.flattened_items) {
            std::cout << "  - " << line << "\n";
        }

        std::cout << "Functions:\n";
        for (const std::string &name : Sorted(annotation.symbols.Functions())) {
            std::cout << "  - " << name << "\n";
        }

        std::cout << "Classes:\n";
        for (const std::string &name : SortedKeys(annotation.class_fields)) {
            std::cout << "  - " << name << ":";
            for (const std::string &field : annotation.class_fields.at(name)) {
                std::cout << " " << field;
            }
            const auto methods_it = annotation.class_methods.find(name);
            if (methods_it != annotation.class_methods.end()) {
                std::cout << " | methods:";
                for (const std::string &method : methods_it->second) {
                    std::cout << " " << method;
                }
            }
            std::cout << "\n";
        }

        std::cout << "Globals:\n";
        for (const std::string &name : Sorted(annotation.symbols.Globals())) {
            std::cout << "  - " << name << "\n";
        }

        std::cout << "Function params:\n";
        for (const std::string &name :
             SortedKeys(annotation.function_parameters)) {
            std::cout << "  - " << name << ":";
            for (const std::string &param :
                 annotation.function_parameters.at(name)) {
                std::cout << " " << param;
            }
            std::cout << "\n";
        }

        std::cout << "Function statements:\n";
        for (const std::string &name :
             SortedKeys(annotation.function_statements)) {
            std::cout << "  - " << name << ":\n";
            for (const std::string &stmt :
                 annotation.function_statements.at(name)) {
                std::cout << "      * " << stmt << "\n";
            }
        }

        std::cout << "Result: " << compiler::interpreter::ValueToString(result)
                  << "\n";
        return 0;
    } catch (const compiler::interpreter::InterpreterException &ex) {
        std::cerr << "interpreter error: " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}
