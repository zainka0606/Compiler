#pragma once

#include "IR.h"

#include <filesystem>
#include <string>
#include <stdexcept>
#include <vector>

namespace compiler::frontend {

class FrontendException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct CompiledBundle {
    compiler::ir::Program program;
    std::vector<compiler::ir::ProgramUnit> prelude_units;
};

struct CompileOptions {
    bool optimize = true;
    std::vector<std::string> disabled_optimization_passes;
};

CompiledBundle CompileEntryFile(const std::filesystem::path &entry_path,
                                const CompileOptions &options);

CompiledBundle CompileEntryFile(const std::filesystem::path &entry_path,
                                bool optimize = true);

void WriteCompiledBundle(const CompiledBundle &bundle,
                         const std::filesystem::path &output_path);

CompiledBundle ReadCompiledBundle(const std::filesystem::path &input_path);

} // namespace compiler::frontend
