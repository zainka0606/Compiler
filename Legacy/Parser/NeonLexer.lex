lexer NeonLexer;
namespace compiler::lang::detail;
token_enum NeonTokenKind;

skip WS = /[ \t\r\n]+/;
token ID = /[A-Za-z_][A-Za-z0-9_]*/;
token NUMBER = /(0[xX][0-9A-Fa-f]+|0[bB][01]+|[0-9]+(\.[0-9]+)?)/;
token SYMBOL = /./;
