#include "ParserGenerator.h"

int main(int argc, char **argv) {
    return compiler::parsergen::RunParserGeneratorCLI(
        argc, const_cast<const char *const *>(argv));
}
