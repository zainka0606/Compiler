lexer Stage2SpecLexer;
namespace compiler::parsergen::detail;
token_enum Stage2SpecTokenKind;

let ALPHA = /[A-Za-z_]/;
let ALNUM = /[A-Za-z0-9_]/;

skip WS = /[ \t\r\n]+/;
skip COMMENT = /#[^\r\n]*/;

token FATARROW = "=>";
token ARROW = "->";
token PIPE = "|";
token SEMI = ";";
token LPAREN = /\(/;
token RPAREN = /\)/;
token LBRACE = /\{/;
token RBRACE = /\}/;
token LBRACKET = /\[/;
token RBRACKET = /\]/;
token COMMA = /,/;
token COLON = /:/;
token DOLLAR = /\$/;
token DOT = /\./;

token KW_GRAMMAR = "grammar";
token KW_START = "start";
token KW_TOKEN = "token";
token KW_RULE = "rule";
token KW_AST = "ast";
token KW_ASTBASE = "astbase";
token KW_LEXEME = "lexeme";
token KW_VIRTUAL = "virtual";
token KW_OVERRIDE = "override";
token KW_CPP = "cpp";

token EQUAL = "=";
token CODE = /`[^`]*`/;

token INT = /[0-9]+/;
token IDENT = /{{ALPHA}}{{ALNUM}}*/;
