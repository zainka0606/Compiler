set(LR0_PARSER_GENERATOR_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/LR0SpecLexer.lex")
set(LR0_PARSER_GENERATOR_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/LR0SpecLexer.lex")
set(LR0_PARSER_GENERATOR_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/LR0SpecLexer")
set(LR0_PARSER_GENERATOR_LEXER_GENERATED_HEADER "${LR0_PARSER_GENERATOR_LEXER_GENERATED_DIR}/LR0SpecLexer.h")
set(LR0_PARSER_GENERATOR_LEXER_GENERATED_SOURCE "${LR0_PARSER_GENERATOR_LEXER_GENERATED_DIR}/LR0SpecLexer.cpp")

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${LR0_PARSER_GENERATOR_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${LR0_PARSER_GENERATOR_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${LR0_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${LR0_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating LR0ParserGenerator lexer from LR0SpecLexer.lex"
)

set_source_files_properties(
        "${LR0_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        "${LR0_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(LR0ParserGeneratorLexerCodegen
        DEPENDS
        "${LR0_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        "${LR0_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
)

add_library(LR0ParserGenerator
        src/Parser.cpp
        src/Generator.cpp
        src/CLI.cpp
        "${LR0_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
)

add_dependencies(LR0ParserGenerator LR0ParserGeneratorLexerCodegen)

target_include_directories(LR0ParserGenerator
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
        "${LR0_PARSER_GENERATOR_LEXER_GENERATED_DIR}"
)

target_link_libraries(LR0ParserGenerator
        PRIVATE
        Common
)

add_library(compiler::LR0ParserGenerator ALIAS LR0ParserGenerator)
add_library(compiler::lr0_parser_generator ALIAS LR0ParserGenerator)

add_executable(LR0ParserGeneratorExe
        src/Main.cpp
)

set_target_properties(LR0ParserGeneratorExe PROPERTIES OUTPUT_NAME LR0ParserGenerator)

target_link_libraries(LR0ParserGeneratorExe
        PRIVATE
        LR0ParserGenerator
)
