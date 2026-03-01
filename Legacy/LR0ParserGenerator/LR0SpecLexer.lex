lexer LR0SpecLexer;
namespace compiler::lr0::detail;
token_enum LR0SpecTokenKind;

let ALPHA = /[A-Za-z_]/;
let ALNUM = /[A-Za-z0-9_]/;

skip WS = /[ \t\r\n]+/;
skip COMMENT = /#[^\r\n]*/;
token KW_GRAMMAR = "grammar";
token KW_START = "start";
token KW_TOKEN = "token";
token KW_RULE = "rule";
token ARROW = "->";
token PIPE = "|";
token SEMI = ";";
token IDENT = /{{ALPHA}}{{ALNUM}}*/;
