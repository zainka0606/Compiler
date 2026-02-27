set(PG2_SPEC_GRAMMAR_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Stage2Spec.pg")
set(PG2_SPEC_GRAMMAR_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Stage2Spec.pg")
set(PG2_SPEC_LEXER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Stage2SpecLexer.lex")
set(PG2_SPEC_LEXER_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Stage2SpecLexer.lex")

# ParserGeneratorStage1 derives the output directory from the input filename stem.
set(PG2_SPEC_PARSER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Stage2Spec")
set(PG2_SPEC_PARSER_GENERATED_HEADER "${PG2_SPEC_PARSER_OUTPUT_DIR}/Stage2SpecParser.h")
set(PG2_SPEC_PARSER_GENERATED_SOURCE "${PG2_SPEC_PARSER_OUTPUT_DIR}/Stage2SpecParser.cpp")
set(PG2_SPEC_PARSER_AST_DOT "${PG2_SPEC_PARSER_OUTPUT_DIR}/AST.dot")
set(PG2_SPEC_PARSER_COLLECTION_DOT "${PG2_SPEC_PARSER_OUTPUT_DIR}/CanonicalCollection.dot")
set(PG2_SPEC_PARSER_TABLE_DOT "${PG2_SPEC_PARSER_OUTPUT_DIR}/ParseTable.dot")

set(PG2_SPEC_LEXER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Stage2SpecLexer")
set(PG2_SPEC_LEXER_GENERATED_HEADER "${PG2_SPEC_LEXER_OUTPUT_DIR}/Stage2SpecLexer.h")
set(PG2_SPEC_LEXER_GENERATED_SOURCE "${PG2_SPEC_LEXER_OUTPUT_DIR}/Stage2SpecLexer.cpp")

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${PG2_SPEC_LEXER_SOURCE}"
        SPEC_BUILD "${PG2_SPEC_LEXER_BUILD}"
        OUTPUT_HEADER "${PG2_SPEC_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${PG2_SPEC_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating ParserGenerator spec lexer from Stage2SpecLexer.lex"
)

compiler_add_parser_generator_stage1_command(
        SPEC_SOURCE "${PG2_SPEC_GRAMMAR_SOURCE}"
        SPEC_BUILD "${PG2_SPEC_GRAMMAR_BUILD}"
        OUTPUT_HEADER "${PG2_SPEC_PARSER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${PG2_SPEC_PARSER_GENERATED_SOURCE}"
        OUTPUT_AST "${PG2_SPEC_PARSER_AST_DOT}"
        OUTPUT_COLLECTION "${PG2_SPEC_PARSER_COLLECTION_DOT}"
        OUTPUT_TABLE "${PG2_SPEC_PARSER_TABLE_DOT}"
        EXTRA_ARGS
        --header "Stage2SpecParser.h"
        --source "Stage2SpecParser.cpp"
        COMMENT "Generating ParserGenerator self spec parser from Stage2Spec.pg using ParserGeneratorStage1"
)

set_source_files_properties(
        "${PG2_SPEC_PARSER_GENERATED_HEADER}"
        "${PG2_SPEC_PARSER_GENERATED_SOURCE}"
        "${PG2_SPEC_PARSER_AST_DOT}"
        "${PG2_SPEC_PARSER_COLLECTION_DOT}"
        "${PG2_SPEC_PARSER_TABLE_DOT}"
        "${PG2_SPEC_LEXER_GENERATED_HEADER}"
        "${PG2_SPEC_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(ParserGeneratorCodegen
        DEPENDS
        "${PG2_SPEC_PARSER_GENERATED_HEADER}"
        "${PG2_SPEC_PARSER_GENERATED_SOURCE}"
        "${PG2_SPEC_PARSER_AST_DOT}"
        "${PG2_SPEC_PARSER_COLLECTION_DOT}"
        "${PG2_SPEC_PARSER_TABLE_DOT}"
        "${PG2_SPEC_LEXER_GENERATED_HEADER}"
        "${PG2_SPEC_LEXER_GENERATED_SOURCE}"
)

add_library(ParserGenerator
        src/Parser.cpp
        src/Generator.cpp
        src/CLI.cpp
        "${PG2_SPEC_PARSER_GENERATED_SOURCE}"
        "${PG2_SPEC_LEXER_GENERATED_SOURCE}"
)

add_dependencies(ParserGenerator ParserGeneratorCodegen)

target_include_directories(ParserGenerator
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
        "${PG2_SPEC_PARSER_OUTPUT_DIR}"
        "${PG2_SPEC_LEXER_OUTPUT_DIR}"
)

target_link_libraries(ParserGenerator
        PUBLIC
        ParserGeneratorStage1
        PRIVATE
        Common
)

add_library(compiler::ParserGenerator ALIAS ParserGenerator)
add_library(compiler::parser_generator ALIAS ParserGenerator)
add_library(compiler::parsergen ALIAS ParserGenerator)
add_library(compiler::ParserGeneratorStage2 ALIAS ParserGenerator)
add_library(compiler::parser_generator_stage2 ALIAS ParserGenerator)
add_library(compiler::parsergen2 ALIAS ParserGenerator)

add_executable(ParserGeneratorExe
        src/Main.cpp
)

set_target_properties(ParserGeneratorExe PROPERTIES OUTPUT_NAME ParserGenerator)

target_link_libraries(ParserGeneratorExe
        PRIVATE
        ParserGenerator
)
