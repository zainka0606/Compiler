#include "Generator.h"

int main(const int argc, const char *const *argv) {
    return compiler::lexgen::RunLexerGeneratorCLI(argc, argv);
}