#include "Common/FileIO.h"
#include "Interpreter.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::string_view TrimAscii(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const char c = text[begin];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin) {
        const char c = text[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        --end;
    }

    return text.substr(begin, end - begin);
}

int BraceDelta(std::string_view line) {
    int delta = 0;
    for (char c : line) {
        if (c == '{') {
            ++delta;
        } else if (c == '}') {
            --delta;
        }
    }
    return delta;
}

bool NeedsStatementTerminator(std::string_view snippet) {
    const std::string_view trimmed = TrimAscii(snippet);
    if (trimmed.empty()) {
        return false;
    }
    const char last = trimmed.back();
    return last != ';' && last != '}';
}

std::vector<std::string> Sorted(std::unordered_set<std::string> values) {
    std::vector<std::string> out(values.begin(), values.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> SortedKeys(
    const std::unordered_map<std::string, std::vector<std::string>> &values) {
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const auto &[key, _] : values) {
        out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void PrintAnnotation(
    const compiler::interpreter::ProgramAnnotation &annotation) {
    std::cout << "Flattened items:\n";
    for (const std::string &item : annotation.flattened_items) {
        std::cout << "  - " << item << "\n";
    }

    std::cout << "Functions:\n";
    for (const std::string &name : Sorted(annotation.symbols.functions)) {
        std::cout << "  - " << name << "\n";
    }

    std::cout << "Globals:\n";
    for (const std::string &name : Sorted(annotation.symbols.globals)) {
        std::cout << "  - " << name << "\n";
    }

    std::cout << "Function params:\n";
    for (const std::string &name : SortedKeys(annotation.function_parameters)) {
        std::cout << "  - " << name << ":";
        for (const std::string &param :
             annotation.function_parameters.at(name)) {
            std::cout << " " << param;
        }
        std::cout << "\n";
    }

    std::cout << "Function statements:\n";
    for (const std::string &name : SortedKeys(annotation.function_statements)) {
        std::cout << "  - " << name << ":\n";
        for (const std::string &stmt :
             annotation.function_statements.at(name)) {
            std::cout << "      * " << stmt << "\n";
        }
    }
}

int RunFileMode(const std::filesystem::path &input_path,
                const std::filesystem::path &ast_dot_path) {
    const std::string source = compiler::common::ReadTextFile(input_path);
    const compiler::interpreter::AST ast =
        compiler::interpreter::ParseProgram(source);
    const compiler::interpreter::ProgramCFG cfg =
        compiler::interpreter::BuildProgramCFG(ast);
    const compiler::interpreter::ProgramAnnotation annotation =
        compiler::interpreter::AnnotateProgram(ast);
    const compiler::interpreter::Value result =
        compiler::interpreter::InterpretProgram(ast);

    const std::filesystem::path cfg_dot_path =
        ast_dot_path.parent_path() /
        (ast_dot_path.stem().string() + ".cfg.dot");
    compiler::common::WriteTextFile(
        ast_dot_path, compiler::interpreter::ASTToGraphvizDot(ast));
    compiler::common::WriteTextFile(
        cfg_dot_path, compiler::interpreter::ProgramCFGToGraphvizDot(cfg));
    std::cout << "Wrote AST DOT to " << ast_dot_path.string() << "\n";
    std::cout << "Wrote CFG DOT to " << cfg_dot_path.string() << "\n";
    PrintAnnotation(annotation);
    std::cout << "Result: " << compiler::interpreter::ValueToString(result)
              << "\n";
    return 0;
}

void PrintHelp() {
    std::cout << "Commands:\n";
    std::cout << "  <statement-or-decl>   execute snippet (semicolon optional "
                 "for simple statements)\n";
    std::cout << "  :help                 show help\n";
    std::cout
        << "  :symbols              show flattened items and symbol table\n";
    std::cout << "  :reset                clear accumulated program\n";
    std::cout << "  exit|quit             quit\n";
}

int RunREPL() {
    std::cout << "Interpreter REPL (MiniLang proof-of-concept)\n";
    std::cout << "Type :help for commands.\n";

    std::string committed_source;
    std::string pending_snippet;
    int pending_brace_balance = 0;

    std::string line;
    while (true) {
        std::cout << (pending_brace_balance == 0 ? "interp> " : "....> ")
                  << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }

        const std::string_view trimmed = TrimAscii(line);
        if (pending_brace_balance == 0) {
            if (trimmed.empty()) {
                continue;
            }
            if (trimmed == "exit" || trimmed == "quit") {
                break;
            }
            if (trimmed == ":help") {
                PrintHelp();
                continue;
            }
            if (trimmed == ":reset") {
                committed_source.clear();
                pending_snippet.clear();
                pending_brace_balance = 0;
                std::cout << "state cleared\n";
                continue;
            }
            if (trimmed == ":symbols") {
                if (committed_source.empty()) {
                    std::cout << "no program state\n";
                    continue;
                }
                try {
                    const compiler::interpreter::AST ast =
                        compiler::interpreter::ParseProgram(committed_source);
                    const compiler::interpreter::ProgramAnnotation annotation =
                        compiler::interpreter::AnnotateProgram(ast);
                    PrintAnnotation(annotation);
                } catch (const std::exception &ex) {
                    std::cout << "error: " << ex.what() << "\n";
                }
                continue;
            }
        }

        pending_snippet += line;
        pending_snippet.push_back('\n');
        pending_brace_balance += BraceDelta(line);

        if (pending_brace_balance < 0) {
            std::cout << "error: unmatched closing brace\n";
            pending_snippet.clear();
            pending_brace_balance = 0;
            continue;
        }

        if (pending_brace_balance > 0) {
            continue;
        }

        std::string snippet = pending_snippet;
        pending_snippet.clear();
        pending_brace_balance = 0;

        if (NeedsStatementTerminator(snippet)) {
            snippet.push_back(';');
            snippet.push_back('\n');
        }

        std::string candidate = committed_source;
        if (!candidate.empty() && candidate.back() != '\n') {
            candidate.push_back('\n');
        }
        candidate += snippet;

        try {
            const compiler::interpreter::AST ast =
                compiler::interpreter::ParseProgram(candidate);
            const compiler::interpreter::Value result =
                compiler::interpreter::InterpretProgram(ast);
            committed_source = std::move(candidate);
            std::cout << "= " << compiler::interpreter::ValueToString(result)
                      << "\n";
        } catch (const std::exception &ex) {
            std::cout << "error: " << ex.what() << "\n";
        }
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 1) {
        try {
            return RunREPL();
        } catch (const std::exception &ex) {
            std::cerr << "error: " << ex.what() << "\n";
            return 1;
        }
    }

    if (argc != 2 && argc != 3) {
        std::cerr << "usage: Interpreter [input-file] [ast-dot-output]\n";
        return 2;
    }

    try {
        const std::filesystem::path input_path = argv[1];
        const std::filesystem::path output_path =
            (argc == 3) ? std::filesystem::path(argv[2]) : "AST.dot";
        return RunFileMode(input_path, output_path);
    } catch (const compiler::interpreter::InterpreterException &ex) {
        std::cerr << "interpreter error: " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}
