add_library(Common
        src/FileIO.cpp
        src/Graphviz.cpp
        src/Identifier.cpp
        src/StringEscape.cpp
)

target_include_directories(Common
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(compiler::Common ALIAS Common)
add_library(compiler::common ALIAS Common)
