function(_compiler_require_args _prefix)
    foreach (_name IN LISTS ARGN)
        if (NOT DEFINED ${_prefix}_${_name} OR "${${_prefix}_${_name}}" STREQUAL "")
            message(FATAL_ERROR "Missing required argument ${_name}")
        endif ()
    endforeach ()
endfunction()

function(compiler_add_copy_file_command)
    set(_options)
    set(_one_value_args OUTPUT SOURCE COMMENT)
    set(_multi_value_args DEPENDS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG OUTPUT SOURCE)

    set(_deps "${ARG_SOURCE}")
    if (ARG_DEPENDS)
        list(APPEND _deps ${ARG_DEPENDS})
    endif ()

    if (NOT ARG_COMMENT)
        set(ARG_COMMENT "Copying ${ARG_SOURCE}")
    endif ()

    add_custom_command(
            OUTPUT
            "${ARG_OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ARG_SOURCE}"
            "${ARG_OUTPUT}"
            DEPENDS
            ${_deps}
            COMMENT "${ARG_COMMENT}"
            VERBATIM
    )
endfunction()

function(_compiler_add_generator_cli_command)
    set(_options)
    set(_one_value_args SPEC_SOURCE SPEC_BUILD EXECUTABLE_TARGET COMMENT)
    set(_multi_value_args OUTPUTS EXTRA_ARGS DEPENDS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG SPEC_SOURCE SPEC_BUILD EXECUTABLE_TARGET COMMENT)
    if (NOT ARG_OUTPUTS)
        message(FATAL_ERROR "Missing required argument OUTPUTS")
    endif ()

    set(_deps "${ARG_EXECUTABLE_TARGET}" "${ARG_SPEC_SOURCE}")
    if (ARG_DEPENDS)
        list(APPEND _deps ${ARG_DEPENDS})
    endif ()

    add_custom_command(
            OUTPUT
            ${ARG_OUTPUTS}
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ARG_SPEC_SOURCE}"
            "${ARG_SPEC_BUILD}"
            COMMAND "$<TARGET_FILE:${ARG_EXECUTABLE_TARGET}>"
            --input "${ARG_SPEC_BUILD}"
            ${ARG_EXTRA_ARGS}
            DEPENDS
            ${_deps}
            COMMENT "${ARG_COMMENT}"
            VERBATIM
    )
endfunction()

function(compiler_add_lexer_generator_command)
    set(_options)
    set(_one_value_args SPEC_SOURCE SPEC_BUILD OUTPUT_HEADER OUTPUT_SOURCE COMMENT)
    set(_multi_value_args EXTRA_ARGS DEPENDS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG SPEC_SOURCE SPEC_BUILD OUTPUT_HEADER OUTPUT_SOURCE COMMENT)

    _compiler_add_generator_cli_command(
            SPEC_SOURCE "${ARG_SPEC_SOURCE}"
            SPEC_BUILD "${ARG_SPEC_BUILD}"
            EXECUTABLE_TARGET LexerGeneratorExe
            OUTPUTS
            "${ARG_OUTPUT_HEADER}"
            "${ARG_OUTPUT_SOURCE}"
            EXTRA_ARGS ${ARG_EXTRA_ARGS}
            DEPENDS ${ARG_DEPENDS}
            COMMENT "${ARG_COMMENT}"
    )
endfunction()

function(compiler_add_lr0_parser_generator_command)
    set(_options)
    set(_one_value_args SPEC_SOURCE SPEC_BUILD OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)
    set(_multi_value_args DEPENDS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG SPEC_SOURCE SPEC_BUILD OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)

    _compiler_add_generator_cli_command(
            SPEC_SOURCE "${ARG_SPEC_SOURCE}"
            SPEC_BUILD "${ARG_SPEC_BUILD}"
            EXECUTABLE_TARGET LR0ParserGeneratorExe
            OUTPUTS
            "${ARG_OUTPUT_AST}"
            "${ARG_OUTPUT_COLLECTION}"
            "${ARG_OUTPUT_TABLE}"
            DEPENDS ${ARG_DEPENDS}
            COMMENT "${ARG_COMMENT}"
    )
endfunction()

function(compiler_add_slr_parser_generator_command)
    set(_options)
    set(_one_value_args SPEC_SOURCE SPEC_BUILD OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)
    set(_multi_value_args DEPENDS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG SPEC_SOURCE SPEC_BUILD OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)

    _compiler_add_generator_cli_command(
            SPEC_SOURCE "${ARG_SPEC_SOURCE}"
            SPEC_BUILD "${ARG_SPEC_BUILD}"
            EXECUTABLE_TARGET SLRParserGeneratorExe
            OUTPUTS
            "${ARG_OUTPUT_AST}"
            "${ARG_OUTPUT_COLLECTION}"
            "${ARG_OUTPUT_TABLE}"
            DEPENDS ${ARG_DEPENDS}
            COMMENT "${ARG_COMMENT}"
    )
endfunction()

function(compiler_add_lr1_parser_generator_command)
    set(_options)
    set(_one_value_args SPEC_SOURCE SPEC_BUILD OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)
    set(_multi_value_args DEPENDS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG SPEC_SOURCE SPEC_BUILD OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)

    _compiler_add_generator_cli_command(
            SPEC_SOURCE "${ARG_SPEC_SOURCE}"
            SPEC_BUILD "${ARG_SPEC_BUILD}"
            EXECUTABLE_TARGET LR1ParserGeneratorExe
            OUTPUTS
            "${ARG_OUTPUT_AST}"
            "${ARG_OUTPUT_COLLECTION}"
            "${ARG_OUTPUT_TABLE}"
            DEPENDS ${ARG_DEPENDS}
            COMMENT "${ARG_COMMENT}"
    )
endfunction()

function(compiler_add_parser_generator_command)
    set(_options)
    set(_one_value_args SPEC_SOURCE SPEC_BUILD OUTPUT_HEADER OUTPUT_SOURCE OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)
    set(_multi_value_args DEPENDS EXTRA_ARGS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG SPEC_SOURCE SPEC_BUILD OUTPUT_HEADER OUTPUT_SOURCE OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)

    _compiler_add_generator_cli_command(
        SPEC_SOURCE "${ARG_SPEC_SOURCE}"
        SPEC_BUILD "${ARG_SPEC_BUILD}"
        EXECUTABLE_TARGET ParserGeneratorExe
        OUTPUTS
            "${ARG_OUTPUT_HEADER}"
            "${ARG_OUTPUT_SOURCE}"
            "${ARG_OUTPUT_AST}"
            "${ARG_OUTPUT_COLLECTION}"
            "${ARG_OUTPUT_TABLE}"
        EXTRA_ARGS ${ARG_EXTRA_ARGS}
        DEPENDS ${ARG_DEPENDS}
        COMMENT "${ARG_COMMENT}"
    )
endfunction()

function(compiler_add_parser_generator_stage1_command)
    set(_options)
    set(_one_value_args SPEC_SOURCE SPEC_BUILD OUTPUT_HEADER OUTPUT_SOURCE OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)
    set(_multi_value_args DEPENDS EXTRA_ARGS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    _compiler_require_args(ARG SPEC_SOURCE SPEC_BUILD OUTPUT_HEADER OUTPUT_SOURCE OUTPUT_AST OUTPUT_COLLECTION OUTPUT_TABLE COMMENT)

    _compiler_add_generator_cli_command(
        SPEC_SOURCE "${ARG_SPEC_SOURCE}"
        SPEC_BUILD "${ARG_SPEC_BUILD}"
        EXECUTABLE_TARGET ParserGeneratorStage1Exe
        OUTPUTS
            "${ARG_OUTPUT_HEADER}"
            "${ARG_OUTPUT_SOURCE}"
            "${ARG_OUTPUT_AST}"
            "${ARG_OUTPUT_COLLECTION}"
            "${ARG_OUTPUT_TABLE}"
        EXTRA_ARGS ${ARG_EXTRA_ARGS}
        DEPENDS ${ARG_DEPENDS}
        COMMENT "${ARG_COMMENT}"
    )
endfunction()

# Backward-compatible alias name from the interim Stage2 split.
function(compiler_add_parser_generator_stage2_command)
    compiler_add_parser_generator_command(${ARGN})
endfunction()
