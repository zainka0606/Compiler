add_library(Bytecode
        src/Bytecode.cpp
        src/passes/PassPipeline.cpp
        src/passes/RegisterAllocationPass.cpp
        src/passes/CallingConventionPass.cpp
        src/passes/RedundantMovePass.cpp
        src/passes/PushPopFoldPass.cpp
        src/passes/LoadStoreForwardingPass.cpp
        src/passes/ConstantBranchPass.cpp
        src/passes/CompareInversionPass.cpp
        src/passes/BranchInversionPass.cpp
        src/passes/JumpPeepholePass.cpp
        src/passes/BranchToReturnPass.cpp
        src/passes/JumpToNextPass.cpp
        src/passes/DeadCodeAfterTerminatorPass.cpp
        src/passes/JumpThreadingPass.cpp
        src/passes/RemoveNopPass.cpp
)

target_include_directories(Bytecode
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(Bytecode
        PUBLIC
        IR
)

add_library(compiler::Bytecode ALIAS Bytecode)
add_library(compiler::bytecode ALIAS Bytecode)
