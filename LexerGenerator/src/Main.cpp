#include "Generator.h"

int main(int argc, const char* const* argv) {
    return compiler::lexgen::RunLexerGeneratorCLI(argc, argv);
}
