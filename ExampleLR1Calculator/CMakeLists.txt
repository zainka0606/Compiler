set(EXAMPLE_LR1_CALCULATOR_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleLR1Calculator.lr1")
set(EXAMPLE_LR1_CALCULATOR_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleLR1Calculator.lr1")
set(EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ExampleInputLexer.lex")
set(EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer.lex")

set(EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/ExampleInputLexer")
set(EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_HEADER "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.h")
set(EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_SOURCE "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_DIR}/ExampleInputLexer.cpp")

compiler_add_copy_file_command(
        OUTPUT "${EXAMPLE_LR1_CALCULATOR_SPEC_BUILD}"
        SOURCE "${EXAMPLE_LR1_CALCULATOR_SPEC_SOURCE}"
        COMMENT "Copying ExampleLR1Calculator grammar spec"
)

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating ExampleLR1Calculator input lexer from ExampleInputLexer.lex"
)

set_source_files_properties(
        "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(ExampleLR1CalculatorSpec
        DEPENDS
        "${EXAMPLE_LR1_CALCULATOR_SPEC_BUILD}"
)

add_custom_target(ExampleLR1CalculatorInputLexerCodegen
        DEPENDS
        "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_HEADER}"
        "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_SOURCE}"
)

add_executable(ExampleLR1Calculator
        Calculator.cpp
        "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_SOURCE}"
)

add_dependencies(ExampleLR1Calculator
        ExampleLR1CalculatorSpec
        ExampleLR1CalculatorInputLexerCodegen
)

target_link_libraries(ExampleLR1Calculator
        PRIVATE
        LR1ParserGenerator
        Common
)

target_include_directories(ExampleLR1Calculator
        PRIVATE
        "${EXAMPLE_LR1_CALCULATOR_INPUT_LEXER_GENERATED_DIR}"
)

add_custom_target(ExampleLR1CalculatorRun
        COMMAND "$<TARGET_FILE:ExampleLR1Calculator>"
        "${EXAMPLE_LR1_CALCULATOR_SPEC_BUILD}"
        DEPENDS
        ExampleLR1Calculator
        ExampleLR1CalculatorSpec
        ExampleLR1CalculatorInputLexerCodegen
        USES_TERMINAL
        VERBATIM
)
