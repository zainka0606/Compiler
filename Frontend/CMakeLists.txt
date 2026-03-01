set(FRONTEND_GRAMMAR_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Neon.pg")
set(FRONTEND_GRAMMAR_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Neon.pg")
set(FRONTEND_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/NeonLexer.lex")
set(FRONTEND_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/NeonLexer.lex")
set(FRONTEND_STDLIB_DIR "${CMAKE_SOURCE_DIR}/StdLib")

# ParserGenerator derives the output directory from the input filename stem:
# Neon.pg -> <binary dir>/Neon/
set(FRONTEND_GRAMMAR_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Neon")
set(FRONTEND_GENERATED_PARSER_HEADER "${FRONTEND_GRAMMAR_OUTPUT_DIR}/GeneratedParser.h")
set(FRONTEND_GENERATED_PARSER_SOURCE "${FRONTEND_GRAMMAR_OUTPUT_DIR}/GeneratedParser.cpp")
set(FRONTEND_GRAMMAR_AST_DOT "${FRONTEND_GRAMMAR_OUTPUT_DIR}/AST.dot")
set(FRONTEND_GRAMMAR_COLLECTION_DOT "${FRONTEND_GRAMMAR_OUTPUT_DIR}/CanonicalCollection.dot")
set(FRONTEND_GRAMMAR_TABLE_DOT "${FRONTEND_GRAMMAR_OUTPUT_DIR}/ParseTable.dot")

set(FRONTEND_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/NeonLexer")
set(FRONTEND_LEXER_GENERATED_HEADER "${FRONTEND_LEXER_GENERATED_DIR}/NeonLexer.h")
set(FRONTEND_LEXER_GENERATED_SOURCE "${FRONTEND_LEXER_GENERATED_DIR}/NeonLexer.cpp")

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${FRONTEND_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${FRONTEND_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${FRONTEND_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${FRONTEND_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating Neon lexer from NeonLexer.lex"
)

compiler_add_parser_generator_command(
        SPEC_SOURCE "${FRONTEND_GRAMMAR_SPEC_SOURCE}"
        SPEC_BUILD "${FRONTEND_GRAMMAR_SPEC_BUILD}"
        OUTPUT_HEADER "${FRONTEND_GENERATED_PARSER_HEADER}"
        OUTPUT_SOURCE "${FRONTEND_GENERATED_PARSER_SOURCE}"
        OUTPUT_AST "${FRONTEND_GRAMMAR_AST_DOT}"
        OUTPUT_COLLECTION "${FRONTEND_GRAMMAR_COLLECTION_DOT}"
        OUTPUT_TABLE "${FRONTEND_GRAMMAR_TABLE_DOT}"
        EXTRA_ARGS
        --header "GeneratedParser.h"
        --source "GeneratedParser.cpp"
        COMMENT "Generating Neon parser and Graphviz dumps from Neon.pg"
)

set_source_files_properties(
        "${FRONTEND_GRAMMAR_AST_DOT}"
        "${FRONTEND_GRAMMAR_COLLECTION_DOT}"
        "${FRONTEND_GRAMMAR_TABLE_DOT}"
        "${FRONTEND_GENERATED_PARSER_HEADER}"
        "${FRONTEND_GENERATED_PARSER_SOURCE}"
        "${FRONTEND_LEXER_GENERATED_HEADER}"
        "${FRONTEND_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(FrontendCodegen
        DEPENDS
        "${FRONTEND_GENERATED_PARSER_HEADER}"
        "${FRONTEND_GENERATED_PARSER_SOURCE}"
        "${FRONTEND_GRAMMAR_AST_DOT}"
        "${FRONTEND_GRAMMAR_COLLECTION_DOT}"
        "${FRONTEND_GRAMMAR_TABLE_DOT}"
)

add_custom_target(FrontendLexerCodegen
        DEPENDS
        "${FRONTEND_LEXER_GENERATED_HEADER}"
        "${FRONTEND_LEXER_GENERATED_SOURCE}"
)

add_library(Frontend
        src/IRBackend.cpp
        src/Parser.cpp
        src/Frontend.cpp
        "${FRONTEND_GENERATED_PARSER_SOURCE}"
        "${FRONTEND_LEXER_GENERATED_SOURCE}"
)

add_dependencies(Frontend
        FrontendCodegen
        FrontendLexerCodegen
)

target_include_directories(Frontend
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${FRONTEND_GRAMMAR_OUTPUT_DIR}"
        PRIVATE
        "${FRONTEND_LEXER_GENERATED_DIR}"
)

target_link_libraries(Frontend
        PUBLIC
        ParserGenerator
        IR
        PRIVATE
        Common
)

target_compile_definitions(Frontend
        PRIVATE
        FRONTEND_STDLIB_DIR="${FRONTEND_STDLIB_DIR}"
)

add_library(compiler::Frontend ALIAS Frontend)
add_library(compiler::frontend ALIAS Frontend)

add_executable(neonc
        src/CompilerMain.cpp
)

target_link_libraries(neonc
        PRIVATE
        Frontend
        Bytecode
        Common
)

target_compile_definitions(neonc
        PRIVATE
        FRONTEND_GRAMMAR_AST_DOT_PATH=\"${FRONTEND_GRAMMAR_AST_DOT}\"
        FRONTEND_GRAMMAR_COLLECTION_DOT_PATH=\"${FRONTEND_GRAMMAR_COLLECTION_DOT}\"
        FRONTEND_GRAMMAR_TABLE_DOT_PATH=\"${FRONTEND_GRAMMAR_TABLE_DOT}\"
)

set_target_properties(neonc PROPERTIES OUTPUT_NAME neonc)
