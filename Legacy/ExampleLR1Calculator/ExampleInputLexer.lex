lexer ExampleInputLexer;
namespace example::lr1input;
token_enum ExampleInputTokenKind;

skip WS = /[ \t\r\n]+/;
token ID = /[A-Za-z_][A-Za-z0-9_]*/;
token NUMBER = /[0-9]+/;
token PLUS = /\+/;
token MINUS = /-/;
token STAR = /\*/;
token SLASH = /\//;
token CARET = /\^/;
token LPAREN = /\(/;
token RPAREN = /\)/;
token COMMA = /,/;
