set(EXAMPLE_LR0_PARSER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleLR0Parser.lr0")
set(EXAMPLE_LR0_PARSER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleLR0Parser.lr0")
set(EXAMPLE_LR0_PARSER_INPUT_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleInputLexer.lex")
set(EXAMPLE_LR0_PARSER_INPUT_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer.lex")
set(EXAMPLE_LR0_PARSER_INPUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Input.neon")
set(EXAMPLE_LR0_PARSER_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Input.neon")

# The LR0ParserGenerator CLI derives the output directory from the input filename stem:
# ExampleLR0Parser.lr0 -> <binary dir>/ExampleLR0Parser/
set(EXAMPLE_LR0_PARSER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleLR0Parser")
set(EXAMPLE_LR0_PARSER_AST_DOT "${EXAMPLE_LR0_PARSER_OUTPUT_DIR}/AST.dot")
set(EXAMPLE_LR0_PARSER_COLLECTION_DOT "${EXAMPLE_LR0_PARSER_OUTPUT_DIR}/CanonicalCollection.dot")
set(EXAMPLE_LR0_PARSER_TABLE_DOT "${EXAMPLE_LR0_PARSER_OUTPUT_DIR}/ParseTable.dot")
set(EXAMPLE_LR0_PARSER_RUNTIME_AST_DOT "${CMAKE_CURRENT_BINARY_DIR}/AST.dot")
set(EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer")
set(EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_HEADER "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.h")
set(EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_SOURCE "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.cpp")

compiler_add_copy_file_command(
        OUTPUT "${EXAMPLE_LR0_PARSER_INPUT_BUILD}"
        SOURCE "${EXAMPLE_LR0_PARSER_INPUT_SOURCE}"
        COMMENT "Copying ExampleLR0Parser input file"
)

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${EXAMPLE_LR0_PARSER_INPUT_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_LR0_PARSER_INPUT_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating ExampleLR0Parser input lexer from ExampleInputLexer.lex"
)

compiler_add_lr0_parser_generator_command(
        SPEC_SOURCE "${EXAMPLE_LR0_PARSER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_LR0_PARSER_SPEC_BUILD}"
        OUTPUT_AST "${EXAMPLE_LR0_PARSER_AST_DOT}"
        OUTPUT_COLLECTION "${EXAMPLE_LR0_PARSER_COLLECTION_DOT}"
        OUTPUT_TABLE "${EXAMPLE_LR0_PARSER_TABLE_DOT}"
        COMMENT "Generating ExampleLR0Parser Graphviz dumps from ExampleLR0Parser.lr0"
)

set_source_files_properties(
        "${EXAMPLE_LR0_PARSER_AST_DOT}"
        "${EXAMPLE_LR0_PARSER_COLLECTION_DOT}"
        "${EXAMPLE_LR0_PARSER_TABLE_DOT}"
        "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(ExampleLR0ParserCodegen
        DEPENDS
        "${EXAMPLE_LR0_PARSER_AST_DOT}"
        "${EXAMPLE_LR0_PARSER_COLLECTION_DOT}"
        "${EXAMPLE_LR0_PARSER_TABLE_DOT}"
)

add_custom_target(ExampleLR0ParserInput
        DEPENDS
        "${EXAMPLE_LR0_PARSER_INPUT_BUILD}"
)

add_custom_target(ExampleLR0ParserInputLexerCodegen
        DEPENDS
        "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
)

add_executable(ExampleLR0ParserDriver
        Driver.cpp
        "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
)

add_dependencies(ExampleLR0ParserDriver ExampleLR0ParserInputLexerCodegen)

target_link_libraries(ExampleLR0ParserDriver
        PRIVATE
        LR0ParserGenerator
        Common
)

target_include_directories(ExampleLR0ParserDriver
        PRIVATE
        "${EXAMPLE_LR0_PARSER_INPUT_LEXER_GENERATED_DIR}"
)

add_custom_target(ExampleLR0ParserRun
        COMMAND "$<TARGET_FILE:ExampleLR0ParserDriver>"
        "${EXAMPLE_LR0_PARSER_SPEC_BUILD}"
        "${EXAMPLE_LR0_PARSER_INPUT_BUILD}"
        "${EXAMPLE_LR0_PARSER_RUNTIME_AST_DOT}"
        DEPENDS
        ExampleLR0ParserDriver
        ExampleLR0ParserCodegen
        ExampleLR0ParserInput
        ExampleLR0ParserInputLexerCodegen
        USES_TERMINAL
        VERBATIM
)
