set(INTERPRETER_GRAMMAR_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Language.pg")
set(INTERPRETER_GRAMMAR_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Language.pg")
set(INTERPRETER_LEXER_SPEC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/LanguageLexer.lex")
set(INTERPRETER_LEXER_SPEC_BUILD "${CMAKE_CURRENT_BINARY_DIR}/LanguageLexer.lex")
set(INTERPRETER_INPUT_SOURCE "${CMAKE_SOURCE_DIR}/Examples/Mandelbrot.txt")
set(INTERPRETER_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/Input.txt")
set(INTERPRETER_STDLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/StdLib")

# ParserGenerator derives the output directory from the input filename stem:
# Language.pg -> <binary dir>/Language/
set(INTERPRETER_GRAMMAR_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Language")
set(INTERPRETER_GENERATED_PARSER_HEADER "${INTERPRETER_GRAMMAR_OUTPUT_DIR}/GeneratedParser.h")
set(INTERPRETER_GENERATED_PARSER_SOURCE "${INTERPRETER_GRAMMAR_OUTPUT_DIR}/GeneratedParser.cpp")
set(INTERPRETER_GRAMMAR_AST_DOT "${INTERPRETER_GRAMMAR_OUTPUT_DIR}/AST.dot")
set(INTERPRETER_GRAMMAR_COLLECTION_DOT "${INTERPRETER_GRAMMAR_OUTPUT_DIR}/CanonicalCollection.dot")
set(INTERPRETER_GRAMMAR_TABLE_DOT "${INTERPRETER_GRAMMAR_OUTPUT_DIR}/ParseTable.dot")

set(INTERPRETER_RUNTIME_AST_DOT "${CMAKE_CURRENT_BINARY_DIR}/AST.dot")

set(INTERPRETER_LEXER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/LanguageLexer")
set(INTERPRETER_LEXER_GENERATED_HEADER "${INTERPRETER_LEXER_GENERATED_DIR}/LanguageLexer.h")
set(INTERPRETER_LEXER_GENERATED_SOURCE "${INTERPRETER_LEXER_GENERATED_DIR}/LanguageLexer.cpp")

compiler_add_copy_file_command(
        OUTPUT "${INTERPRETER_INPUT_BUILD}"
        SOURCE "${INTERPRETER_INPUT_SOURCE}"
        COMMENT "Copying Interpreter input file"
)

compiler_add_lexer_generator_command(
        SPEC_SOURCE "${INTERPRETER_LEXER_SPEC_SOURCE}"
        SPEC_BUILD "${INTERPRETER_LEXER_SPEC_BUILD}"
        OUTPUT_HEADER "${INTERPRETER_LEXER_GENERATED_HEADER}"
        OUTPUT_SOURCE "${INTERPRETER_LEXER_GENERATED_SOURCE}"
        COMMENT "Generating Interpreter language lexer from LanguageLexer.lex"
)

compiler_add_parser_generator_command(
        SPEC_SOURCE "${INTERPRETER_GRAMMAR_SPEC_SOURCE}"
        SPEC_BUILD "${INTERPRETER_GRAMMAR_SPEC_BUILD}"
        OUTPUT_HEADER "${INTERPRETER_GENERATED_PARSER_HEADER}"
        OUTPUT_SOURCE "${INTERPRETER_GENERATED_PARSER_SOURCE}"
        OUTPUT_AST "${INTERPRETER_GRAMMAR_AST_DOT}"
        OUTPUT_COLLECTION "${INTERPRETER_GRAMMAR_COLLECTION_DOT}"
        OUTPUT_TABLE "${INTERPRETER_GRAMMAR_TABLE_DOT}"
        EXTRA_ARGS
        --header "GeneratedParser.h"
        --source "GeneratedParser.cpp"
        COMMENT "Generating Interpreter parser and Graphviz dumps from Language.pg"
)

set_source_files_properties(
        "${INTERPRETER_GRAMMAR_AST_DOT}"
        "${INTERPRETER_GRAMMAR_COLLECTION_DOT}"
        "${INTERPRETER_GRAMMAR_TABLE_DOT}"
        "${INTERPRETER_GENERATED_PARSER_HEADER}"
        "${INTERPRETER_GENERATED_PARSER_SOURCE}"
        "${INTERPRETER_LEXER_GENERATED_HEADER}"
        "${INTERPRETER_LEXER_GENERATED_SOURCE}"
        PROPERTIES GENERATED TRUE
)

add_custom_target(InterpreterCodegen
        DEPENDS
        "${INTERPRETER_GENERATED_PARSER_HEADER}"
        "${INTERPRETER_GENERATED_PARSER_SOURCE}"
        "${INTERPRETER_GRAMMAR_AST_DOT}"
        "${INTERPRETER_GRAMMAR_COLLECTION_DOT}"
        "${INTERPRETER_GRAMMAR_TABLE_DOT}"
)

add_custom_target(InterpreterInput
        DEPENDS
        "${INTERPRETER_INPUT_BUILD}"
)

add_custom_target(InterpreterLexerCodegen
        DEPENDS
        "${INTERPRETER_LEXER_GENERATED_HEADER}"
        "${INTERPRETER_LEXER_GENERATED_SOURCE}"
)

add_library(Interpreter
        src/SymbolTable.cpp
        src/CFG.cpp
        src/Interpreter.cpp
        "${INTERPRETER_GENERATED_PARSER_SOURCE}"
        "${INTERPRETER_LEXER_GENERATED_SOURCE}"
)

add_dependencies(Interpreter
        InterpreterCodegen
        InterpreterLexerCodegen
)

target_include_directories(Interpreter
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${INTERPRETER_GRAMMAR_OUTPUT_DIR}"
        PRIVATE
        "${INTERPRETER_LEXER_GENERATED_DIR}"
)

target_link_libraries(Interpreter
        PUBLIC
        ParserGenerator
        PRIVATE
        Common
)

target_compile_definitions(Interpreter
        PRIVATE
        INTERPRETER_STDLIB_DIR=\"${INTERPRETER_STDLIB_DIR}\"
)

add_library(compiler::Interpreter ALIAS Interpreter)
add_library(compiler::interpreter ALIAS Interpreter)

add_executable(InterpreterExe
        src/Main.cpp
)

set_target_properties(InterpreterExe PROPERTIES OUTPUT_NAME Interpreter)

target_link_libraries(InterpreterExe
        PRIVATE
        Interpreter
        Common
)

add_custom_target(InterpreterRun
        COMMAND "$<TARGET_FILE:InterpreterExe>"
        DEPENDS
        InterpreterExe
        InterpreterCodegen
        InterpreterLexerCodegen
        USES_TERMINAL
        VERBATIM
)

add_executable(InterpreterAstTestExe
        tests/AstTest.cpp
)

set_target_properties(InterpreterAstTestExe PROPERTIES OUTPUT_NAME InterpreterAstTest)

target_link_libraries(InterpreterAstTestExe
        PRIVATE
        Interpreter
        Common
)

add_custom_target(InterpreterAstTestRun
        COMMAND "$<TARGET_FILE:InterpreterAstTestExe>"
        "${INTERPRETER_INPUT_BUILD}"
        "${INTERPRETER_RUNTIME_AST_DOT}"
        DEPENDS
        InterpreterAstTestExe
        InterpreterCodegen
        InterpreterInput
        USES_TERMINAL
        VERBATIM
)
