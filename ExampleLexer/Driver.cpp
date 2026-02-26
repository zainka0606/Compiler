#include "ExampleLexer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

const char* ToString(example::lang::ExampleTokenKind kind) {
    switch (kind) {
        case example::lang::ExampleTokenKind::IF:
            return "IF";
        case example::lang::ExampleTokenKind::IDENT:
            return "IDENT";
        case example::lang::ExampleTokenKind::INT:
            return "INT";
        case example::lang::ExampleTokenKind::PLUS:
            return "PLUS";
        case example::lang::ExampleTokenKind::EndOfFile:
            return "EndOfFile";
    }
    return "<unknown>";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ExampleLexerDriver <input>\n";
        return 2;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::cerr << "failed to open input file: " << argv[1] << "\n";
        return 3;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        std::cerr << "failed to read input file: " << argv[1] << "\n";
        return 4;
    }

    const std::string input = buffer.str();
    example::lang::ExampleLexer lexer(input);
    const auto tokens = lexer.Tokenize();

    for (const auto& token : tokens) {
        std::cout << ToString(token.kind) << "|" << token.lexeme << "|" << token.line << ":" << token.column << "\n";
    }

    return 0;
}
