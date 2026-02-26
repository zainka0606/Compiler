set(PARSER_GRAMMAR_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Language.pg")
set(PARSER_GRAMMAR_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Language.pg")
set(PARSER_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/LanguageLexer.lex")
set(PARSER_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/LanguageLexer.lex")
set(PARSER_INPUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Input.txt")
set(PARSER_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Input.txt")

# ParserGenerator derives the output directory from the input filename stem:
# Language.pg -> <binary dir>/Language/
set(PARSER_GRAMMAR_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Language")
set(PARSER_GENERATED_PARSER_HEADER "${PARSER_GRAMMAR_OUTPUT_DIR}/GeneratedParser.h")
set(PARSER_GENERATED_PARSER_SOURCE "${PARSER_GRAMMAR_OUTPUT_DIR}/GeneratedParser.cpp")
set(PARSER_GRAMMAR_AST_DOT "${PARSER_GRAMMAR_OUTPUT_DIR}/AST.dot")
set(PARSER_GRAMMAR_COLLECTION_DOT "${PARSER_GRAMMAR_OUTPUT_DIR}/CanonicalCollection.dot")
set(PARSER_GRAMMAR_TABLE_DOT "${PARSER_GRAMMAR_OUTPUT_DIR}/ParseTable.dot")

set(PARSER_RUNTIME_AST_DOT "${CMAKE_CURRENT_BINARY_DIR}/AST.dot")

set(PARSER_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/LanguageLexer")
set(PARSER_LEXER_GENERATED_HEADER "${PARSER_LEXER_GENERATED_DIR}/LanguageLexer.h")
set(PARSER_LEXER_GENERATED_SOURCE "${PARSER_LEXER_GENERATED_DIR}/LanguageLexer.cpp")

compiler_add_copy_file_command(
    OUTPUT "${PARSER_INPUT_BUILD}"
    SOURCE "${PARSER_INPUT_SOURCE}"
    COMMENT "Copying Parser input file"
)

compiler_add_lexer_generator_command(
    SPEC_SOURCE "${PARSER_LEXER_SPEC_SOURCE}"
    SPEC_BUILD "${PARSER_LEXER_SPEC_BUILD}"
    OUTPUT_HEADER "${PARSER_LEXER_GENERATED_HEADER}"
    OUTPUT_SOURCE "${PARSER_LEXER_GENERATED_SOURCE}"
    COMMENT "Generating Parser language lexer from LanguageLexer.lex"
)

compiler_add_parser_generator_command(
    SPEC_SOURCE "${PARSER_GRAMMAR_SPEC_SOURCE}"
    SPEC_BUILD "${PARSER_GRAMMAR_SPEC_BUILD}"
    OUTPUT_HEADER "${PARSER_GENERATED_PARSER_HEADER}"
    OUTPUT_SOURCE "${PARSER_GENERATED_PARSER_SOURCE}"
    OUTPUT_AST "${PARSER_GRAMMAR_AST_DOT}"
    OUTPUT_COLLECTION "${PARSER_GRAMMAR_COLLECTION_DOT}"
    OUTPUT_TABLE "${PARSER_GRAMMAR_TABLE_DOT}"
    EXTRA_ARGS
        --header "GeneratedParser.h"
        --source "GeneratedParser.cpp"
    COMMENT "Generating Parser parser and Graphviz dumps from Language.pg"
)

set_source_files_properties(
    "${PARSER_GRAMMAR_AST_DOT}"
    "${PARSER_GRAMMAR_COLLECTION_DOT}"
    "${PARSER_GRAMMAR_TABLE_DOT}"
    "${PARSER_GENERATED_PARSER_HEADER}"
    "${PARSER_GENERATED_PARSER_SOURCE}"
    "${PARSER_LEXER_GENERATED_HEADER}"
    "${PARSER_LEXER_GENERATED_SOURCE}"
    PROPERTIES GENERATED TRUE
)

add_custom_target(ParserCodegen
    DEPENDS
        "${PARSER_GENERATED_PARSER_HEADER}"
        "${PARSER_GENERATED_PARSER_SOURCE}"
        "${PARSER_GRAMMAR_AST_DOT}"
        "${PARSER_GRAMMAR_COLLECTION_DOT}"
        "${PARSER_GRAMMAR_TABLE_DOT}"
)

add_custom_target(ParserInput
    DEPENDS
        "${PARSER_INPUT_BUILD}"
)

add_custom_target(ParserLexerCodegen
    DEPENDS
        "${PARSER_LEXER_GENERATED_HEADER}"
        "${PARSER_LEXER_GENERATED_SOURCE}"
)

add_library(Parser
    src/Parser.cpp
    "${PARSER_GENERATED_PARSER_SOURCE}"
    "${PARSER_LEXER_GENERATED_SOURCE}"
)

add_dependencies(Parser
    ParserCodegen
    ParserLexerCodegen
)

target_include_directories(Parser
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PARSER_GRAMMAR_OUTPUT_DIR}"
    PRIVATE
        "${PARSER_LEXER_GENERATED_DIR}"
)

target_link_libraries(Parser
    PUBLIC
        ParserGenerator
    PRIVATE
        Common
)

add_library(compiler::Parser ALIAS Parser)
add_library(compiler::parser ALIAS Parser)

add_executable(ParserAstTestExe
    tests/AstTest.cpp
)

set_target_properties(ParserAstTestExe PROPERTIES OUTPUT_NAME ParserAstTest)

target_link_libraries(ParserAstTestExe
    PRIVATE
        Parser
        Common
)

add_custom_target(ParserAstTestRun
    COMMAND "$<TARGET_FILE:ParserAstTestExe>"
        "${PARSER_INPUT_BUILD}"
        "${PARSER_RUNTIME_AST_DOT}"
    DEPENDS
        ParserAstTestExe
        ParserCodegen
        ParserInput
    USES_TERMINAL
    VERBATIM
)
