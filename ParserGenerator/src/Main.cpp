#include "ParserGenerator.h"

int main(const int argc, char **argv) {
    return compiler::parsergen::RunParserGeneratorCLI(
        argc, argv);
}
