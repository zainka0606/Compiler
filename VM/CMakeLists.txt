add_library(VM
        src/VM.cpp
)

target_include_directories(VM
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(VM
        PUBLIC
        Bytecode
        IR
        PRIVATE
        Common
)

add_library(compiler::VM ALIAS VM)
add_library(compiler::vm ALIAS VM)

add_executable(VMExe
        src/Main.cpp
)

set_target_properties(VMExe PROPERTIES OUTPUT_NAME neon)

target_link_libraries(VMExe
        PRIVATE
        VM
        Bytecode
        Common
)
