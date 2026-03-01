#include "PassFactories.h"

namespace compiler::ir {

std::vector<std::unique_ptr<Pass>> BuildDefaultPassPipeline() {
    std::vector<std::unique_ptr<Pass>> passes;
    passes.push_back(CreateResolveObjectAccessPass());
    passes.push_back(CreateDevirtualizeCallPass());
    passes.push_back(CreateInliningPass());
    passes.push_back(CreateIdentityArithmeticPass());
    passes.push_back(CreateConstantFoldingPass());
    passes.push_back(CreateCopyPropagationPass());
    passes.push_back(CreateCommonSubexpressionEliminationPass());
    passes.push_back(CreateLoadStoreForwardingPass());
    passes.push_back(CreateDeadLocalStoreEliminationPass());
    passes.push_back(CreateCompareInversionPass());
    passes.push_back(CreateBranchInversionPass());
    passes.push_back(CreateBranchSimplificationPass());
    passes.push_back(CreateControlFlowCleanupPass());
    passes.push_back(CreateLinearBlockMergePass());
    passes.push_back(CreateRedundantMovePass());
    passes.push_back(CreateDeadTempEliminationPass());
    passes.push_back(CreateUnreachableBlockEliminationPass());
    return passes;
}

} // namespace compiler::ir
