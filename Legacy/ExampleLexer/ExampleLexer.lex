lexer ExampleLexer;
namespace example::lang;
token_enum ExampleTokenKind;

let DIGIT = /[0-9]/;
let LETTER = /[A-Za-z_]/;

skip WS = /[ \t\r\n]+/;
token IF = "if";
token IDENT = /{{LETTER}}({{LETTER}}|{{DIGIT}})*/;
token INT = /{{DIGIT}}+/;
token PLUS = /\+/;
