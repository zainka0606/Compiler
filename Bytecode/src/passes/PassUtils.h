#pragma once

#include "Bytecode.h"

namespace compiler::bytecode {

inline bool IsJumpOpcode(const OpCode opcode) {
    return opcode == OpCode::Jump || opcode == OpCode::JumpIfFalse ||
           opcode == OpCode::JumpCarry || opcode == OpCode::JumpNotCarry ||
           opcode == OpCode::JumpZero || opcode == OpCode::JumpNotZero ||
           opcode == OpCode::JumpSign || opcode == OpCode::JumpNotSign ||
           opcode == OpCode::JumpOverflow ||
           opcode == OpCode::JumpNotOverflow || opcode == OpCode::JumpAbove ||
           opcode == OpCode::JumpAboveEqual || opcode == OpCode::JumpBelow ||
           opcode == OpCode::JumpBelowEqual || opcode == OpCode::JumpGreater ||
           opcode == OpCode::JumpGreaterEqual || opcode == OpCode::JumpLess ||
           opcode == OpCode::JumpLessEqual;
}

inline bool IsConditionalJumpOpcode(const OpCode opcode) {
    return opcode == OpCode::JumpIfFalse || opcode == OpCode::JumpCarry ||
           opcode == OpCode::JumpNotCarry || opcode == OpCode::JumpZero ||
           opcode == OpCode::JumpNotZero || opcode == OpCode::JumpSign ||
           opcode == OpCode::JumpNotSign || opcode == OpCode::JumpOverflow ||
           opcode == OpCode::JumpNotOverflow || opcode == OpCode::JumpAbove ||
           opcode == OpCode::JumpAboveEqual || opcode == OpCode::JumpBelow ||
           opcode == OpCode::JumpBelowEqual || opcode == OpCode::JumpGreater ||
           opcode == OpCode::JumpGreaterEqual || opcode == OpCode::JumpLess ||
           opcode == OpCode::JumpLessEqual;
}

} // namespace compiler::bytecode
