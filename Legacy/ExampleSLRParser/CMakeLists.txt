set(EXAMPLE_SLR_PARSER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleSLRParser.slr")
set(EXAMPLE_SLR_PARSER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleSLRParser.slr")
set(EXAMPLE_SLR_PARSER_INPUT_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleInputLexer.lex")
set(EXAMPLE_SLR_PARSER_INPUT_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer.lex")
set(EXAMPLE_SLR_PARSER_INPUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Input.neon")
set(EXAMPLE_SLR_PARSER_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Input.neon")

# The SLRParserGenerator CLI derives the output directory from the input filename stem:
# ExampleSLRParser.slr -> <binary dir>/ExampleSLRParser/
set(EXAMPLE_SLR_PARSER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleSLRParser")
set(EXAMPLE_SLR_PARSER_AST_DOT "${EXAMPLE_SLR_PARSER_OUTPUT_DIR}/AST.dot")
set(EXAMPLE_SLR_PARSER_COLLECTION_DOT "${EXAMPLE_SLR_PARSER_OUTPUT_DIR}/CanonicalCollection.dot")
set(EXAMPLE_SLR_PARSER_TABLE_DOT "${EXAMPLE_SLR_PARSER_OUTPUT_DIR}/ParseTable.dot")
set(EXAMPLE_SLR_PARSER_RUNTIME_AST_DOT "${CMAKE_CURRENT_BINARY_DIR}/AST.dot")
set(EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer")
set(EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_HEADER "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.h")
set(EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_SOURCE "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.cpp")

compiler_add_copy_file_command(
        OUTPUT "${EXAMPLE_SLR_PARSER_INPUT_BUILD}"
        SOURCE "${EXAMPLE_SLR_PARSER_INPUT_SOURCE}"
        COMMENT "Copying ExampleSLRParser input file"
)

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${EXAMPLE_SLR_PARSER_INPUT_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_SLR_PARSER_INPUT_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating ExampleSLRParser input lexer from ExampleInputLexer.lex"
)

compiler_add_slr_parser_generator_command(
        SPEC_SOURCE "${EXAMPLE_SLR_PARSER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_SLR_PARSER_SPEC_BUILD}"
        OUTPUT_AST "${EXAMPLE_SLR_PARSER_AST_DOT}"
        OUTPUT_COLLECTION "${EXAMPLE_SLR_PARSER_COLLECTION_DOT}"
        OUTPUT_TABLE "${EXAMPLE_SLR_PARSER_TABLE_DOT}"
        COMMENT "Generating ExampleSLRParser Graphviz dumps from ExampleSLRParser.slr"
)

set_source_files_properties(
        "${EXAMPLE_SLR_PARSER_AST_DOT}"
        "${EXAMPLE_SLR_PARSER_COLLECTION_DOT}"
        "${EXAMPLE_SLR_PARSER_TABLE_DOT}"
        "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(ExampleSLRParserCodegen
        DEPENDS
        "${EXAMPLE_SLR_PARSER_AST_DOT}"
        "${EXAMPLE_SLR_PARSER_COLLECTION_DOT}"
        "${EXAMPLE_SLR_PARSER_TABLE_DOT}"
)

add_custom_target(ExampleSLRParserInput
        DEPENDS
        "${EXAMPLE_SLR_PARSER_INPUT_BUILD}"
)

add_custom_target(ExampleSLRParserInputLexerCodegen
        DEPENDS
        "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
)

add_executable(ExampleSLRParserDriver
        Driver.cpp
        "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_SOURCE}"
)

add_dependencies(ExampleSLRParserDriver ExampleSLRParserInputLexerCodegen)

target_link_libraries(ExampleSLRParserDriver
        PRIVATE
        SLRParserGenerator
        Common
)

target_include_directories(ExampleSLRParserDriver
        PRIVATE
        "${EXAMPLE_SLR_PARSER_INPUT_LEXER_GENERATED_DIR}"
)

add_custom_target(ExampleSLRParserRun
        COMMAND "$<TARGET_FILE:ExampleSLRParserDriver>"
        "${EXAMPLE_SLR_PARSER_SPEC_BUILD}"
        "${EXAMPLE_SLR_PARSER_INPUT_BUILD}"
        "${EXAMPLE_SLR_PARSER_RUNTIME_AST_DOT}"
        DEPENDS
        ExampleSLRParserDriver
        ExampleSLRParserCodegen
        ExampleSLRParserInput
        ExampleSLRParserInputLexerCodegen
        USES_TERMINAL
        VERBATIM
)
