#include "PassFactories.h"

namespace compiler::bytecode {

std::vector<std::unique_ptr<Pass>> BuildDefaultPassPipeline() {
    std::vector<std::unique_ptr<Pass>> passes;
    passes.push_back(CreateRegisterAllocationPass());
    passes.push_back(CreateCallingConventionPass());
    passes.push_back(CreateRedundantMovePass());
    passes.push_back(CreateConstantBranchPass());
    passes.push_back(CreateCompareInversionPass());
    passes.push_back(CreateBranchInversionPass());
    passes.push_back(CreateJumpPeepholePass());
    passes.push_back(CreateJumpThreadingPass());
    passes.push_back(CreateBranchToReturnPass());
    passes.push_back(CreateJumpToNextPass());
    passes.push_back(CreateDeadCodeAfterTerminatorPass());
    passes.push_back(CreateRemoveNopPass());
    return passes;
}

} // namespace compiler::bytecode
