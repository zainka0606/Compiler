set(EXAMPLE_LR1_PARSER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleLR1Parser.lr1")
set(EXAMPLE_LR1_PARSER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleLR1Parser.lr1")
set(EXAMPLE_LR1_PARSER_INPUT_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleInputLexer.lex")
set(EXAMPLE_LR1_PARSER_INPUT_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer.lex")
set(EXAMPLE_LR1_PARSER_INPUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Input.neon")
set(EXAMPLE_LR1_PARSER_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Input.neon")

# The LR1ParserGenerator CLI derives the output directory from the input filename stem:
# ExampleLR1Parser.lr1 -> <binary dir>/ExampleLR1Parser/
set(EXAMPLE_LR1_PARSER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleLR1Parser")
set(EXAMPLE_LR1_PARSER_AST_DOT "${EXAMPLE_LR1_PARSER_OUTPUT_DIR}/AST.dot")
set(EXAMPLE_LR1_PARSER_COLLECTION_DOT "${EXAMPLE_LR1_PARSER_OUTPUT_DIR}/CanonicalCollection.dot")
set(EXAMPLE_LR1_PARSER_TABLE_DOT "${EXAMPLE_LR1_PARSER_OUTPUT_DIR}/ParseTable.dot")
set(EXAMPLE_LR1_PARSER_RUNTIME_AST_DOT "${CMAKE_CURRENT_BINARY_DIR}/AST.dot")
set(EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer")
set(EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_HEADER "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.h")
set(EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_SOURCE "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.cpp")

compiler_add_copy_file_command(
        OUTPUT "${EXAMPLE_LR1_PARSER_INPUT_BUILD}"
        SOURCE "${EXAMPLE_LR1_PARSER_INPUT_SOURCE}"
        COMMENT "Copying ExampleLR1Parser input file"
)

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${EXAMPLE_LR1_PARSER_INPUT_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_LR1_PARSER_INPUT_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating ExampleLR1Parser input lexer from ExampleInputLexer.lex"
)

compiler_add_lr1_parser_generator_command(
        SPEC_SOURCE "${EXAMPLE_LR1_PARSER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_LR1_PARSER_SPEC_BUILD}"
        OUTPUT_AST "${EXAMPLE_LR1_PARSER_AST_DOT}"
        OUTPUT_COLLECTION "${EXAMPLE_LR1_PARSER_COLLECTION_DOT}"
        OUTPUT_TABLE "${EXAMPLE_LR1_PARSER_TABLE_DOT}"
        COMMENT "Generating ExampleLR1Parser Graphviz dumps from ExampleLR1Parser.lr1"
)

set_source_files_properties(
        "${EXAMPLE_LR1_PARSER_AST_DOT}"
        "${EXAMPLE_LR1_PARSER_COLLECTION_DOT}"
        "${EXAMPLE_LR1_PARSER_TABLE_DOT}"
        "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(ExampleLR1ParserCodegen
        DEPENDS
        "${EXAMPLE_LR1_PARSER_AST_DOT}"
        "${EXAMPLE_LR1_PARSER_COLLECTION_DOT}"
        "${EXAMPLE_LR1_PARSER_TABLE_DOT}"
)

add_custom_target(ExampleLR1ParserInput
        DEPENDS
        "${EXAMPLE_LR1_PARSER_INPUT_BUILD}"
)

add_custom_target(ExampleLR1ParserInputLexerCodegen
        DEPENDS
        "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
)

add_executable(ExampleLR1ParserDriver
        Driver.cpp
        "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
)

add_dependencies(ExampleLR1ParserDriver ExampleLR1ParserInputLexerCodegen)

target_link_libraries(ExampleLR1ParserDriver
        PRIVATE
        LR1ParserGenerator
        Common
)

target_include_directories(ExampleLR1ParserDriver
        PRIVATE
        "${EXAMPLE_LR1_PARSER_INPUT_LEXER_GENERATED_DIR}"
)

add_custom_target(ExampleLR1ParserRun
        COMMAND "$<TARGET_FILE:ExampleLR1ParserDriver>"
        "${EXAMPLE_LR1_PARSER_SPEC_BUILD}"
        "${EXAMPLE_LR1_PARSER_INPUT_BUILD}"
        "${EXAMPLE_LR1_PARSER_RUNTIME_AST_DOT}"
        DEPENDS
        ExampleLR1ParserDriver
        ExampleLR1ParserCodegen
        ExampleLR1ParserInput
        ExampleLR1ParserInputLexerCodegen
        USES_TERMINAL
        VERBATIM
)
