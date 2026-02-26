add_library(LexerGenerator
        src/Parser.cpp
        src/Generator.cpp
        src/CLI.cpp
)

target_include_directories(LexerGenerator
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(LexerGenerator
        PUBLIC
        Regex
        PRIVATE
        Common
)

add_library(compiler::LexerGenerator ALIAS LexerGenerator)
add_library(compiler::generator ALIAS LexerGenerator)
add_library(compiler::lexer_generator ALIAS LexerGenerator)

add_executable(LexerGeneratorExe
        src/Main.cpp
)

set_target_properties(LexerGeneratorExe PROPERTIES OUTPUT_NAME LexerGenerator)

target_link_libraries(LexerGeneratorExe
        PRIVATE
        LexerGenerator
)
