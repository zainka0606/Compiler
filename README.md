# Compiler

This repository is a multi-stage compiler/tooling playground with:

- regex parsing + NFA/DFA/minimization primitives
- a lexer generator
- LR-family parser generators (`LR0`, `SLR`, `LR1`)
- a staged parser generator pipeline (`ParserGeneratorStage1` -> `ParserGenerator`)
- generated-parser consumers (`Parser`, `Example*` modules)
- an interactive interpreter with AST + CFG dumps

## Top-Level Modules

- `Common` - shared utilities and CMake helpers (including generator custom-command wrappers)
- `Regex` - regex parser + NFA/DFA/minimizer libraries and tests
- `LexerGenerator` - `.lex` -> generated C++ lexer (+ optional AST/NFA/DFA Graphviz dumps)
- `LR0ParserGenerator` - `.lr0` grammar analysis dumps (`AST`, canonical collection, parse table)
- `SLRParserGenerator` - `.slr` grammar analysis dumps
- `LR1ParserGenerator` - `.lr1` grammar analysis dumps
- `ParserGeneratorStage1` - stage-1 generic parser generator
- `ParserGenerator` - stage-2 generator (typed AST, inline C++ rule actions)
- `Parser` - parser module built from `ParserGenerator`
- `Interpreter` - parser+runtime module with REPL/file mode and CFG generation
- `ExampleLexer`, `ExampleLR0Parser`, `ExampleSLRParser`, `ExampleLR1Parser`, `ExampleLR1Calculator`,
  `ExampleCalculator` - runnable reference modules

## Build

The project is commonly built in `cmake-build-debug`:

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --parallel
ctest --test-dir cmake-build-debug --output-on-failure
```

All binaries are emitted under:

- `cmake-build-debug/bin`

## File Type Conventions

- Lexer specs: `*.lex`
- LR(0) specs: `*.lr0`
- SLR specs: `*.slr`
- LR(1) specs: `*.lr1`
- ParserGenerator (stage-2) specs: `*.pg`

## Generator CLI Quick Start

Output directories are derived from the input filename stem.
Example: `Foo.lex` -> `Foo/`, `Grammar.lr1` -> `Grammar/`.

### LexerGenerator

```bash
./cmake-build-debug/bin/LexerGenerator --input path/to/Test.lex \
  --dump-ast --dump-nfa --dump-dfa
```

Typical outputs in `Test/`:

- `AST.dot`
- `NFA/*.dot`
- `DFA.dot`
- generated lexer `.h/.cpp` (default names come from the lexer declaration)

### LR0 / SLR / LR1 parser generators

```bash
./cmake-build-debug/bin/LR0ParserGenerator --input path/to/Grammar.lr0
./cmake-build-debug/bin/SLRParserGenerator --input path/to/Grammar.slr
./cmake-build-debug/bin/LR1ParserGenerator --input path/to/Grammar.lr1
```

Each writes:

- `AST.dot`
- `CanonicalCollection.dot`
- `ParseTable.dot`

### ParserGeneratorStage1 / ParserGenerator

```bash
./cmake-build-debug/bin/ParserGeneratorStage1 --input path/to/Spec.lr1
./cmake-build-debug/bin/ParserGenerator --input path/to/Spec.pg
```

`ParserGenerator` supports:

- typed AST declarations
- list-typed fields (`Type[]`)
- inline C++ rule actions (`=> cpp \`...\``)

## Interpreter

Binary:

```bash
./cmake-build-debug/bin/Interpreter
```

Modes:

- no args: interactive REPL
- file mode: `Interpreter <input-file> [ast-dot-output]`

In file mode it writes:

- AST dot file (path from arg or `AST.dot`)
- CFG dot file alongside it (`<stem>.cfg.dot`)

### Supported language features

- literals: number, string, character, boolean
- expressions:
    - arithmetic: `+ - * / ^`
    - comparisons: `== != < <= > >=`
    - ternary: `cond ? a : b`
    - function calls
- statements:
    - `let`
    - expression statement
    - `return`
    - `if / else`
    - `while`
    - `for (init; condition; update)`
- user-defined functions
- builtin math functions: `sin`, `cos`, `tan`, `sqrt`, `abs`, `exp`, `ln`, `log10`, `pow`, `min`, `max`, `sum`
- builtin IO functions: `print`, `println`, `readln`, `input`

### Interpreter sample run target

```bash
cmake --build cmake-build-debug --target InterpreterAstTestRun
```

## Example Targets

Useful ready-to-run custom targets:

- `ExampleLexerRun`
- `ExampleLR0ParserRun`
- `ExampleSLRParserRun`
- `ExampleLR1ParserRun`
- `ExampleLR1CalculatorRun`
- `ExampleCalculatorRun`
- `ExampleCalculatorSampleRun`
- `ParserAstTestRun`
- `InterpreterRun`
- `InterpreterAstTestRun`
