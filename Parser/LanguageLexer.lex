lexer LanguageLexer;
namespace compiler::lang::detail;
token_enum LanguageTokenKind;

skip WS = /[ \t\r\n]+/;
token ID = /[A-Za-z_][A-Za-z0-9_]*/;
token NUMBER = /[0-9]+(\.[0-9]+)?/;
token SYMBOL = /./;
