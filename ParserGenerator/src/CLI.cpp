#include "Common/FileIO.h"
#include "ParserGenerator.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace compiler::parsergen {

namespace {

struct CLIOptions {
    bool show_help = false;
    std::filesystem::path input_path;
    std::string header_filename;
    std::string source_filename;
};

std::string UsageText(const std::string_view program) {
    std::ostringstream oss;
    oss << "Usage: " << program << " --input <stage2-spec> [options]\n\n";
    oss << "Outputs (written to a directory derived from the input filename "
           "stem):\n";
    oss << "  <Parser>.h / <Parser>.cpp   Generated parser (CST + action-built "
           "AST)\n";
    oss << "  AST.dot                     Parsed Stage2 spec AST (Graphviz)\n";
    oss << "  CanonicalCollection.dot     LR(1) canonical collection "
           "(Graphviz)\n";
    oss << "  ParseTable.dot              LR(1) ACTION/GOTO table "
           "(Graphviz)\n\n";
    oss << "Options:\n";
    oss << "  --input, -i <file>    Input Stage2 parser spec\n";
    oss << "  --header <file>       Generated header filename override\n";
    oss << "  --source <file>       Generated source filename override\n";
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
        } else if (arg == "--header") {
            options.header_filename = require_value(arg);
        } else if (arg == "--source") {
            options.source_filename = require_value(arg);
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

void WriteTextFile(const std::filesystem::path &path,
                   const std::string_view text) {
    try {
        common::WriteTextFile(path, text);
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

} // namespace

int RunParserGeneratorCLI(int argc, const char *const *argv) {
    const std::string program =
        argc > 0 && argv && argv[0] ? argv[0] : "ParserGenerator";

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

        const Stage2SpecAST spec = ParseStage2Spec(source);
        const LR1ParseTable table = BuildLR1ParseTable(spec);
        const GeneratedParserFiles generated = GenerateCppParser(
            spec, source, options.header_filename, options.source_filename);

        std::filesystem::create_directories(output_dir);
        WriteTextFile(output_dir / generated.header_filename,
                      generated.header_source);
        WriteTextFile(output_dir / generated.source_filename,
                      generated.implementation_source);
        WriteTextFile(output_dir / "AST.dot", Stage2SpecASTToGraphvizDot(spec));
        WriteTextFile(output_dir / "CanonicalCollection.dot",
                      parsergen::LR1CanonicalCollectionToGraphvizDot(
                          table.canonical_collection));
        WriteTextFile(output_dir / "ParseTable.dot",
                      parsergen::LR1ParseTableToGraphvizDot(table));

        std::cout << "Wrote ParserGenerator outputs to " << output_dir.string()
                  << "\n";
        std::cout << "Parser: " << generated.header_filename << ", "
                  << generated.source_filename << "\n";
        std::cout << "States: " << table.canonical_collection.states.size()
                  << ", conflicts: " << table.conflicts.size() << "\n";
        return 0;
    } catch (const ParseException &ex) {
        std::cerr << "parse error at " << ex.line() << ":" << ex.column()
                  << ": " << ex.what() << "\n";
    } catch (const BuildException &ex) {
        std::cerr << "ParserGenerator error: " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "unexpected error: " << ex.what() << "\n";
    }

    return 1;
}

} // namespace compiler::parsergen
