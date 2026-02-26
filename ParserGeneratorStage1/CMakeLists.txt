add_library(ParserGeneratorStage1
    src/Parser.cpp
    src/CST.cpp
    src/Generator.cpp
    src/CLI.cpp
)

target_include_directories(ParserGeneratorStage1
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(ParserGeneratorStage1
    PUBLIC
        LR1ParserGenerator
    PRIVATE
        Common
)

add_library(compiler::ParserGeneratorStage1 ALIAS ParserGeneratorStage1)
add_library(compiler::parser_generator_stage1 ALIAS ParserGeneratorStage1)
add_library(compiler::parsergen_stage1 ALIAS ParserGeneratorStage1)

add_executable(ParserGeneratorStage1Exe
    src/Main.cpp
)

set_target_properties(ParserGeneratorStage1Exe PROPERTIES OUTPUT_NAME ParserGeneratorStage1)

target_link_libraries(ParserGeneratorStage1Exe
    PRIVATE
        ParserGeneratorStage1
)
