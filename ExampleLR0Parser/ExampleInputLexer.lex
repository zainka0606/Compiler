lexer ExampleInputLexer;
namespace example::lr0input;
token_enum ExampleInputTokenKind;

skip WS = /[ \t\r\n]+/;
token ID = /[A-Za-z_][A-Za-z0-9_]*/;
token PLUS = /\+/;
