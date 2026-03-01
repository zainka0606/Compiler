add_library(IR
        src/IR.cpp
        src/IRBinary.cpp
        src/passes/PassPipeline.cpp
        src/passes/ResolveObjectAccessPass.cpp
        src/passes/DevirtualizeCallPass.cpp
        src/passes/InliningPass.cpp
        src/passes/IdentityArithmeticPass.cpp
        src/passes/ConstantFoldingPass.cpp
        src/passes/CopyPropagationPass.cpp
        src/passes/CommonSubexpressionEliminationPass.cpp
        src/passes/LoadStoreForwardingPass.cpp
        src/passes/DeadLocalStoreEliminationPass.cpp
        src/passes/CompareInversionPass.cpp
        src/passes/BranchInversionPass.cpp
        src/passes/BranchSimplificationPass.cpp
        src/passes/ControlFlowCleanupPass.cpp
        src/passes/LinearBlockMergePass.cpp
        src/passes/RedundantMovePass.cpp
        src/passes/DeadTempEliminationPass.cpp
        src/passes/UnreachableBlockEliminationPass.cpp
)

target_include_directories(IR
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(IR
        PUBLIC
        Common
)

add_library(compiler::IR ALIAS IR)
add_library(compiler::ir ALIAS IR)
