#pragma once

#include "Bytecode.h"
#include "Pass.h"

#include <memory>
#include <vector>

namespace compiler::bytecode {

std::unique_ptr<Pass> CreateRegisterAllocationPass();
std::unique_ptr<Pass> CreateCallingConventionPass();
std::unique_ptr<Pass> CreateRedundantMovePass();
std::unique_ptr<Pass> CreatePushPopFoldPass();
std::unique_ptr<Pass> CreateLoadStoreForwardingPass();
std::unique_ptr<Pass> CreateConstantBranchPass();
std::unique_ptr<Pass> CreateCompareInversionPass();
std::unique_ptr<Pass> CreateBranchInversionPass();
std::unique_ptr<Pass> CreateJumpPeepholePass();
std::unique_ptr<Pass> CreateBranchToReturnPass();
std::unique_ptr<Pass> CreateJumpToNextPass();
std::unique_ptr<Pass> CreateDeadCodeAfterTerminatorPass();
std::unique_ptr<Pass> CreateJumpThreadingPass();
std::unique_ptr<Pass> CreateRemoveNopPass();

std::vector<std::unique_ptr<Pass>> BuildDefaultPassPipeline();

} // namespace compiler::bytecode
