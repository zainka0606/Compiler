set(LR1_PARSER_GENERATOR_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/LR1SpecLexer.lex")
set(LR1_PARSER_GENERATOR_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/LR1SpecLexer.lex")
set(LR1_PARSER_GENERATOR_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/LR1SpecLexer")
set(LR1_PARSER_GENERATOR_LEXER_GENERATED_HEADER "${LR1_PARSER_GENERATOR_LEXER_GENERATED_DIR}/LR1SpecLexer.h")
set(LR1_PARSER_GENERATOR_LEXER_GENERATED_SOURCE "${LR1_PARSER_GENERATOR_LEXER_GENERATED_DIR}/LR1SpecLexer.cpp")

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${LR1_PARSER_GENERATOR_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${LR1_PARSER_GENERATOR_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${LR1_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${LR1_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating LR1ParserGenerator lexer from LR1SpecLexer.lex"
)

set_source_files_properties(
        "${LR1_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        "${LR1_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(LR1ParserGeneratorLexerCodegen
        DEPENDS
        "${LR1_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        "${LR1_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
)

add_library(LR1ParserGenerator
        src/Parser.cpp
        src/Generator.cpp
        src/CLI.cpp
        "${LR1_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
)

add_dependencies(LR1ParserGenerator LR1ParserGeneratorLexerCodegen)

target_include_directories(LR1ParserGenerator
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
        "${LR1_PARSER_GENERATOR_LEXER_GENERATED_DIR}"
)

target_link_libraries(LR1ParserGenerator
        PRIVATE
        Common
)

add_library(compiler::LR1ParserGenerator ALIAS LR1ParserGenerator)
add_library(compiler::lr1_parser_generator ALIAS LR1ParserGenerator)

add_executable(LR1ParserGeneratorExe
        src/Main.cpp
)

set_target_properties(LR1ParserGeneratorExe PROPERTIES OUTPUT_NAME LR1ParserGenerator)

target_link_libraries(LR1ParserGeneratorExe
        PRIVATE
        LR1ParserGenerator
)
