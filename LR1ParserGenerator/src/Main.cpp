#include "LR1ParserGenerator.h"

int main(int argc, const char *const *argv) {
    return compiler::lr1::RunLR1ParserGeneratorCLI(argc, argv);
}