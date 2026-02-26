#include "Common/FileIO.h"
#include "Parser.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: ParserAstTest <input> [ast-dot-output]\n";
        return 2;
    }

    try {
        const std::filesystem::path input_path = argv[1];
        const std::filesystem::path output_path = (argc == 3) ? std::filesystem::path(argv[2]) : "AST.dot";

        const std::string source = compiler::common::ReadTextFile(input_path);
        const compiler::lang::AST ast = compiler::lang::ParseProgram(source);
        compiler::common::WriteTextFile(output_path, compiler::lang::ASTToGraphvizDot(ast));

        std::cout << "Wrote AST DOT to " << output_path.string() << "\n";
        return 0;
    } catch (const compiler::lang::ParserException& ex) {
        std::cerr << "parser error: " << ex.what() << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}
