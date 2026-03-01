#include "Bytecode.h"
#include "VM.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage() { std::cout << "usage: neon <program.bin> [--dump-asm]\n"; }

} // namespace

int main(const int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        PrintUsage();
        return 2;
    }

    bool dump_asm = false;
    if (argc == 3) {
        const std::string flag = argv[2];
        if (flag == "--dump-asm") {
            dump_asm = true;
        } else {
            PrintUsage();
            return 2;
        }
    }

    const std::filesystem::path input_path = argv[1];

    try {
        const compiler::bytecode::ProgramBundle bundle =
            compiler::bytecode::ReadProgramBundleBinary(input_path);

        if (dump_asm) {
            std::cout << compiler::bytecode::ProgramBundleToAssembly(bundle,
                                                                     "<program>")
                      << "\n";
        }

        const compiler::vm::Value result =
            compiler::vm::ExecuteProgram(bundle.program, bundle.prelude_units);
        std::cout << compiler::vm::ValueToString(result) << "\n";
        return 0;
    } catch (const compiler::bytecode::BytecodeException &ex) {
        std::cerr << "bytecode error: " << ex.what() << "\n";
    } catch (const compiler::vm::VMException &ex) {
        std::cerr << "vm error: " << ex.what() << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    return 1;
}
