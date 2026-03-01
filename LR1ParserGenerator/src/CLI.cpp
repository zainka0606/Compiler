#include "Common/FileIO.h"
#include "LR1ParserGenerator.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace compiler::lr1 {
namespace {
struct CLIOptions {
    bool show_help = false;
    std::filesystem::path input_path;
};

std::string UsageText(const std::string_view program) {
    std::ostringstream oss;
    oss << "Usage: " << program << " --input <grammar-spec>\n\n";
    oss << "Outputs (written to a directory derived from the input filename "
           "stem):\n";
    oss << "  AST.dot               Parsed grammar AST (Graphviz)\n";
    oss << "  CanonicalCollection.dot  LR(1) canonical collection (Graphviz)\n";
    oss << "  ParseTable.dot        LR(1) ACTION/GOTO table with conflicts "
           "(Graphviz)\n\n";
    oss << "Options:\n";
    oss << "  --input, -i <file>    Input grammar spec\n";
    oss << "  -h, --help            Show help\n";
    return oss.str();
}

CLIOptions ParseCLIOptions(const int argc, const char *const *argv) {
    CLIOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto require_value = [&](const std::string &flag) -> std::string {
            if (i + 1 >= argc) {
                throw BuildException("missing value for option " + flag);
            }
            ++i;
            return argv[i];
        };

        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
        } else if (arg == "--input" || arg == "-i") {
            options.input_path = require_value(arg);
        } else {
            throw BuildException("unknown option: " + arg);
        }
    }

    return options;
}

std::string ReadTextFile(const std::filesystem::path &path) {
    try {
        return common::ReadTextFile(path);
    } catch (const std::exception &ex) {
        throw BuildException(ex.what());
    }
}

std::filesystem::path
DeriveOutputDirectory(const std::filesystem::path &input_path) {
    const std::filesystem::path stem = input_path.stem();
    if (stem.empty()) {
        throw BuildException(
            "failed to derive output directory from input file: " +
            input_path.string());
    }
    if (input_path.has_parent_path()) {
        return input_path.parent_path() / stem;
    }
    return stem;
}

void WriteTextFile(const std::filesystem::path &path,
                   const std::string_view text) {
    try {
        common::WriteTextFile(path, text);
    } catch (const std::exception &ex) {
        throw BuildException(ex.what());
    }
}
} // namespace

int RunLR1ParserGeneratorCLI(int argc, const char *const *argv) {
    const std::string program =
        argc > 0 && argv && argv[0] ? argv[0] : "LR1ParserGenerator";

    try {
        const CLIOptions options = ParseCLIOptions(argc, argv);
        if (options.show_help) {
            std::cout << UsageText(program);
            return 0;
        }

        if (options.input_path.empty()) {
            throw BuildException("missing required option --input");
        }

        const std::filesystem::path output_dir =
            DeriveOutputDirectory(options.input_path);
        const std::string source = ReadTextFile(options.input_path);

        const GrammarSpecAST spec = ParseGrammarSpec(source);
        const LR1ParseTable table = BuildLR1ParseTable(spec);

        std::filesystem::create_directories(output_dir);
        WriteTextFile(output_dir / "AST.dot",
                      GrammarSpecASTToGraphvizDot(spec));
        WriteTextFile(
            output_dir / "CanonicalCollection.dot",
            LR1CanonicalCollectionToGraphvizDot(table.canonical_collection));
        WriteTextFile(output_dir / "ParseTable.dot",
                      LR1ParseTableToGraphvizDot(table));

        std::cout << "Wrote LR1 dumps to " << output_dir.string() << "\n";
        std::cout << "States: " << table.canonical_collection.states.size()
                  << ", conflicts: " << table.conflicts.size() << "\n";
        return 0;
    } catch (const ParseException &ex) {
        std::cerr << "parse error at " << ex.line() << ":" << ex.column()
                  << ": " << ex.what() << "\n";
    } catch (const BuildException &ex) {
        std::cerr << "LR1ParserGenerator error: " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "unexpected error: " << ex.what() << "\n";
    }

    return 1;
}
} // namespace compiler::lr1