add_library(Regex
    src/Parser.cpp
    src/NFA.cpp
    src/DFA.cpp
    src/DFAMinimizer.cpp
)

target_include_directories(Regex
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(compiler::Regex ALIAS Regex)
add_library(compiler::regex ALIAS Regex)

if(BUILD_TESTING)
    add_executable(ParserTests
        tests/ParserTests.cpp
    )
    target_link_libraries(ParserTests PRIVATE Regex)
    add_test(NAME ParserTests COMMAND ParserTests)

    add_executable(NFATests
        tests/NFATests.cpp
    )
    target_link_libraries(NFATests PRIVATE Regex)
    add_test(NAME NFATests COMMAND NFATests)

    add_executable(DFATests
        tests/DFATests.cpp
    )
    target_link_libraries(DFATests PRIVATE Regex)
    add_test(NAME DFATests COMMAND DFATests)

    add_executable(DFAMinimizerTests
        tests/DFAMinimizerTests.cpp
    )
    target_link_libraries(DFAMinimizerTests PRIVATE Regex)
    add_test(NAME DFAMinimizerTests COMMAND DFAMinimizerTests)
endif()
