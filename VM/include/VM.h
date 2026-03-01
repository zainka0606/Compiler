#pragma once

#include "Bytecode.h"
#include "IR.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace compiler::vm {

class VMException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

using Value = compiler::ir::Value;

Value ExecuteProgram(
    const compiler::bytecode::Program &program,
    const std::vector<compiler::bytecode::ProgramUnit> &prelude_units = {});

std::string ValueToString(const Value &value);

} // namespace compiler::vm
