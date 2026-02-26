#include "LR0ParserGenerator.h"

int main(int argc, const char *const *argv) {
    return compiler::lr0::RunLR0ParserGeneratorCLI(argc, argv);
}