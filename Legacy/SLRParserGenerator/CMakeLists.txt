set(SLR_PARSER_GENERATOR_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/SLRSpecLexer.lex")
set(SLR_PARSER_GENERATOR_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/SLRSpecLexer.lex")
set(SLR_PARSER_GENERATOR_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/SLRSpecLexer")
set(SLR_PARSER_GENERATOR_LEXER_GENERATED_HEADER "${SLR_PARSER_GENERATOR_LEXER_GENERATED_DIR}/SLRSpecLexer.h")
set(SLR_PARSER_GENERATOR_LEXER_GENERATED_SOURCE "${SLR_PARSER_GENERATOR_LEXER_GENERATED_DIR}/SLRSpecLexer.cpp")

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${SLR_PARSER_GENERATOR_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${SLR_PARSER_GENERATOR_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${SLR_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${SLR_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating SLRParserGenerator lexer from SLRSpecLexer.lex"
)

set_source_files_properties(
        "${SLR_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        "${SLR_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(SLRParserGeneratorLexerCodegen
        DEPENDS
        "${SLR_PARSER_GENERATOR_LEXER_GENERATED_HEADER}"
        "${SLR_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
)

add_library(SLRParserGenerator
        src/Parser.cpp
        src/Generator.cpp
        src/CLI.cpp
        "${SLR_PARSER_GENERATOR_LEXER_GENERATED_SOURCE}"
)

add_dependencies(SLRParserGenerator SLRParserGeneratorLexerCodegen)

target_include_directories(SLRParserGenerator
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
        "${SLR_PARSER_GENERATOR_LEXER_GENERATED_DIR}"
)

target_link_libraries(SLRParserGenerator
        PRIVATE
        Common
)

add_library(compiler::SLRParserGenerator ALIAS SLRParserGenerator)
add_library(compiler::slr_parser_generator ALIAS SLRParserGenerator)

add_executable(SLRParserGeneratorExe
        src/Main.cpp
)

set_target_properties(SLRParserGeneratorExe PROPERTIES OUTPUT_NAME SLRParserGenerator)

target_link_libraries(SLRParserGeneratorExe
        PRIVATE
        SLRParserGenerator
)
