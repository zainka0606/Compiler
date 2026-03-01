set(EXAMPLE_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleLexer.lex")
set(EXAMPLE_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleLexer.lex")
set(EXAMPLE_LEXER_INPUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Input.neon")
set(EXAMPLE_LEXER_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Input.neon")

# The LexerGenerator CLI derives the output directory from the input file stem:
# ExampleLexer.lex -> <binary dir>/ExampleLexer/
set(EXAMPLE_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleLexer")
set(EXAMPLE_LEXER_GENERATED_HEADER "${EXAMPLE_LEXER_GENERATED_DIR}/ExampleLexer.h")
set(EXAMPLE_LEXER_GENERATED_SOURCE "${EXAMPLE_LEXER_GENERATED_DIR}/ExampleLexer.cpp")

compiler_add_copy_file_command(
        OUTPUT "${EXAMPLE_LEXER_INPUT_BUILD}"
        SOURCE "${EXAMPLE_LEXER_INPUT_SOURCE}"
        COMMENT "Copying ExampleLexer input file"
)

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${EXAMPLE_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${EXAMPLE_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${EXAMPLE_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating ExampleLexer sources from ExampleLexer.lex"
)

set_source_files_properties(
        "${EXAMPLE_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(ExampleLexerCodegen
        DEPENDS
        "${EXAMPLE_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LEXER_GENERATED_SOURCE}"
)

add_custom_target(ExampleLexerInput
        DEPENDS
        "${EXAMPLE_LEXER_INPUT_BUILD}"
)

add_executable(ExampleLexerDriver
        Driver.cpp
        "${EXAMPLE_LEXER_GENERATED_SOURCE}"
)

add_dependencies(ExampleLexerDriver ExampleLexerCodegen)

target_link_libraries(ExampleLexerDriver
        PRIVATE
        Common
)

target_include_directories(ExampleLexerDriver
        PRIVATE
        "${EXAMPLE_LEXER_GENERATED_DIR}"
)

add_custom_target(ExampleLexerRun
        COMMAND "$<TARGET_FILE:ExampleLexerDriver>" "${EXAMPLE_LEXER_INPUT_BUILD}"
        DEPENDS
        ExampleLexerDriver
        ExampleLexerCodegen
        ExampleLexerInput
        USES_TERMINAL
        VERBATIM
)
