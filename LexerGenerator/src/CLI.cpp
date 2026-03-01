#include "Common/FileIO.h"
#include "Common/Identifier.h"
#include "Generator.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace compiler::lexgen {
namespace {
struct CLIOptions {
    bool show_help = false;
    std::filesystem::path input_path;
    std::string header_filename;
    std::string source_filename;
    bool dump_ast = false;
    bool dump_nfa = false;
    bool dump_dfa = false;
};

std::string UsageText(const std::string_view program) {
    std::ostringstream oss;
    oss << "Usage: " << program << " --input <spec.lex> [options]\n\n";
    oss << "Options:\n";
    oss << "  Output directory is always derived from the input filename stem "
           "(for example Test.lex -> Test/)\n";
    oss << "  --header <file>        Generated header filename (default: "
           "<LexerName>.h)\n";
    oss << "  --source <file>        Generated source filename (default: "
           "<LexerName>.cpp)\n";
    oss << "  --dump-ast             Write AST.dot in the derived output "
           "directory\n";
    oss << "  --dump-nfa             Write per-rule NFA .dot files in "
           "<out>/NFA\n";
    oss << "  --dump-dfa             Write combined lexer DFA .dot file to "
           "<out>/DFA.dot\n";
    oss << "  -h, --help             Show help\n";
    return oss.str();
}

CLIOptions ParseCLIOptions(const int argc, const char *const *argv) {
    CLIOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const std::string &flag) -> std::string {
            if (i + 1 >= argc) {
                throw LexerCompileException("missing value for option " + flag);
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
        } else if (arg == "--dump-ast") {
            options.dump_ast = true;
        } else if (arg == "--dump-nfa") {
            options.dump_nfa = true;
        } else if (arg == "--dump-dfa") {
            options.dump_dfa = true;
        } else {
            throw LexerCompileException("unknown option: " + arg);
        }
    }

    return options;
}

std::string ReadTextFile(const std::filesystem::path &path) {
    try {
        return common::ReadTextFile(path);
    } catch (const std::exception &ex) {
        throw LexerCompileException(ex.what());
    }
}

std::filesystem::path
DeriveOutputDirectory(const std::filesystem::path &input_path) {
    const std::filesystem::path stem = input_path.stem();
    if (stem.empty()) {
        throw LexerCompileException(
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
        throw LexerCompileException(ex.what());
    }
}
} // namespace

int RunLexerGeneratorCLI(int argc, const char *const *argv) {
    const std::string program =
        argc > 0 && argv && argv[0] ? argv[0] : "LexerGenerator";

    try {
        const CLIOptions options = ParseCLIOptions(argc, argv);

        if (options.show_help) {
            std::cout << UsageText(program);
            return 0;
        }

        if (options.input_path.empty()) {
            throw LexerCompileException("missing required option --input");
        }
        const std::filesystem::path output_dir =
            DeriveOutputDirectory(options.input_path);

        const std::string spec_text = ReadTextFile(options.input_path);
        const CompiledLexer compiled = CompileLexerSpec(spec_text);
        const GeneratedLexerFiles generated = GenerateCppLexer(
            compiled, options.header_filename, options.source_filename);

        std::filesystem::create_directories(output_dir);
        WriteTextFile(output_dir / generated.header_filename,
                      generated.header_source);
        WriteTextFile(output_dir / generated.source_filename,
                      generated.implementation_source);

        if (options.dump_ast) {
            WriteTextFile(output_dir / "AST.dot",
                          BuildCompiledASTDot(compiled));
        }

        if (options.dump_nfa) {
            const std::filesystem::path nfa_dir = output_dir / "NFA";
            std::filesystem::create_directories(nfa_dir);
            for (const auto &rule : compiled.rules) {
                std::ostringstream filename;
                filename << std::setw(2) << std::setfill('0') << rule.rule_index
                         << "_"
                         << common::SanitizeIdentifier(rule.name,
                                                                 "rule")
                         << ".dot";
                WriteTextFile(
                    nfa_dir / filename.str(),
                    NFAToGraphvizDot(
                        rule.nfa, "NFA_" + common::SanitizeIdentifier(
                                               rule.name, "rule")));
            }
        }

        if (options.dump_dfa) {
            WriteTextFile(output_dir / "DFA.dot",
                          CombinedDFAToGraphvizDot(compiled, "LexerDFA"));
        }

        return 0;
    } catch (const SpecParseException &ex) {
        std::cerr << "spec parse error at " << ex.line() << ":" << ex.column()
                  << ": " << ex.what() << "\n";
    } catch (const LexerCompileException &ex) {
        std::cerr << "LexerGenerator error: " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "unexpected error: " << ex.what() << "\n";
    }

    return 1;
}
} // namespace compiler::lexgen