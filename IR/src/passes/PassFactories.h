#pragma once

#include "Pass.h"

#include <memory>
#include <vector>

namespace compiler::ir {

std::unique_ptr<Pass> CreateResolveObjectAccessPass();
std::unique_ptr<Pass> CreateDevirtualizeCallPass();
std::unique_ptr<Pass> CreateInliningPass();
std::unique_ptr<Pass> CreateIdentityArithmeticPass();
std::unique_ptr<Pass> CreateConstantFoldingPass();
std::unique_ptr<Pass> CreateCopyPropagationPass();
std::unique_ptr<Pass> CreateCommonSubexpressionEliminationPass();
std::unique_ptr<Pass> CreateLoadStoreForwardingPass();
std::unique_ptr<Pass> CreateDeadLocalStoreEliminationPass();
std::unique_ptr<Pass> CreateCompareInversionPass();
std::unique_ptr<Pass> CreateBranchInversionPass();
std::unique_ptr<Pass> CreateBranchSimplificationPass();
std::unique_ptr<Pass> CreateControlFlowCleanupPass();
std::unique_ptr<Pass> CreateLinearBlockMergePass();
std::unique_ptr<Pass> CreateRedundantMovePass();
std::unique_ptr<Pass> CreateDeadTempEliminationPass();
std::unique_ptr<Pass> CreateUnreachableBlockEliminationPass();

std::vector<std::unique_ptr<Pass>> BuildDefaultPassPipeline();

} // namespace compiler::ir
