# Compiler (Custom Language)

This repository includes a working first-version lexer generator (`LexerGenerator`) built with a handwritten
lexer/parser for its own spec language.
It also includes a top-down (recursive-descent) regex parser, Thompson-style NFA construction, NFA->DFA conversion, and
DFA minimization.

## Project Layout

- `CMakeLists.txt` - top-level build config (uses `add_subdirectory(Regex)` and `add_subdirectory(LexerGenerator)`)
- `Regex/CMakeLists.txt` - regex library + regex test targets
- `Regex/include/Parser.h` - public AST and parser API
- `Regex/include/NFA.h` - NFA data structures, compiler, and NFA matcher
- `Regex/include/DFA.h` - DFA compiler and matcher
- `Regex/include/DFAMinimizer.h` - DFA minimization
- `Regex/src/Parser.cpp` - recursive-descent regex parser implementation
- `Regex/src/NFA.cpp` - regex AST to NFA conversion (Thompson construction)
- `Regex/src/DFA.cpp` - NFA to DFA conversion
- `Regex/src/DFAMinimizer.cpp` - DFA minimization
- `Regex/tests/*.cpp` - regex parser/NFA/DFA/minimizer tests
- `LexerGenerator/CMakeLists.txt` - lexer generator library, CLI, and tests
- `LexerGenerator/include/Generator.h` - lexer spec parser, compiler, codegen, and debug dump APIs
- `LexerGenerator/src/Parser.cpp` - handwritten lexer/parser for lexer generator specs
- `LexerGenerator/src/Generator.cpp` - spec compilation, code generation, tokenization, and debug dumps
- `LexerGenerator/src/CLI.cpp` / `LexerGenerator/src/Main.cpp` - `LexerGenerator` command-line tool

## Supported Regex Syntax (Current)

- Literals (for example `a`, `b`, `1`)
- Concatenation (for example `ab`)
- Alternation (`|`)
- Grouping with parentheses (`( ... )`)
- Wildcard dot (`.`)
- Quantifiers: `*`, `+`, `?`, `{m}`, `{m,}`, `{m,n}`
- Character classes: `[abc]`, `[a-z]`, `[^a-z]`
- Basic escapes: `\n`, `\t`, `\r`, `\\`, and escaping metacharacters (for example `\.`)

## Build And Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Lexer Generator (`LexerGenerator`)

The generator parses a small handwritten spec language and emits a table-driven C++ lexer.

Example spec:

```text
lexer DemoLexer;
namespace demo::lang;
token_enum DemoTokenKind;

let DIGIT = /[0-9]/;
skip WS = /[ \t\r\n]+/;
token INT = /{{DIGIT}}+/;
token PLUS = /\+/;
```

Generate code:

```bash
LexerGenerator --input Test.lex
```

Debug dumps (written under a derived output directory named after the input file stem):

```bash
LexerGenerator --input Test.lex \
  --dump-ast \
  --dump-nfa \
  --dump-dfa
```

Generated debug files (for `Test.lex`, output directory is `Test/`):

- `Test/AST.dot`
- `Test/NFA/*.dot`
- `Test/DFA.dot` (combined lexer DFA)

## Notes

- The parser returns an AST (`RegexNode`) and throws `compiler::regex::ParseException` on invalid input.
- `CompileToNFA(...)` / `CompilePatternToNFA(...)` produce an epsilon-NFA and `NFAMatches(...)` runs it.
- `CompileNFAToDFA(...)`, `MinimizeDFA(...)`, and the lexer generator build on top of those automata primitives.
- The lexer generator builds a combined multi-rule DFA for lexing (longest-match, then rule-order priority).
