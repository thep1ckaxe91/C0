%{
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <string>
#include "c0/ast.h"
#include "c0/driver.h"
#include "parser.tab.hpp"

#define YY_EXTRA_TYPE c0::Driver*

#define YY_USER_ACTION \
    yylloc->first_line = yylineno; \
    yylloc->first_column = yyextra->column; \
    yylloc->last_line = yylineno; \
    yylloc->last_column = yyextra->column + yyleng - 1; \
    yyextra->column += yyleng;

static bool parse_c0_string(const char* text, size_t len, std::string& out, std::string& err) {
    out.clear();
    out.reserve(len);
    for (size_t i = 1; i + 1 < len; ++i) {
        if (text[i] == '\\') {
            ++i;
            if (i + 1 >= len) break;
            char c = text[i];
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'v': out.push_back('\v'); break;
                case 'b': out.push_back('\b'); break;
                case 'r': out.push_back('\r'); break;
                case 'f': out.push_back('\f'); break;
                case 'a': out.push_back('\a'); break;
                case '\\': out.push_back('\\'); break;
                case '\'': out.push_back('\''); break;
                case '"': out.push_back('"'); break;
                case '0':
                    err = "Null character '\\0' not allowed in string literal";
                    return false;
                default:
                    err = std::string("Invalid escape sequence: \\") + c;
                    return false;
            }
        } else {
            out.push_back(text[i]);
        }
    }
    return true;
}

static bool parse_c0_char(const char* text, size_t len, char& out, std::string& err) {
    if (len < 3) {
        err = "Empty character literal";
        return false;
    }
    if (text[1] == '\\') {
        if (len != 4) {
            err = "Character literal contains multiple characters";
            return false;
        }
        char c = text[2];
        switch (c) {
            case 'n': out = '\n'; return true;
            case 't': out = '\t'; return true;
            case 'v': out = '\v'; return true;
            case 'b': out = '\b'; return true;
            case 'r': out = '\r'; return true;
            case 'f': out = '\f'; return true;
            case 'a': out = '\a'; return true;
            case '\\': out = '\\'; return true;
            case '\'': out = '\''; return true;
            case '"': out = '"'; return true;
            case '0': out = '\0'; return true;
            default:
                err = std::string("Invalid escape sequence in character: \\") + c;
                return false;
        }
    } else {
        if (len != 3) {
            err = "Character literal contains multiple characters";
            return false;
        }
        out = text[1];
        return true;
    }
}

%}

%option noyywrap
%option reentrant
%option bison-bridge
%option bison-locations
%option yylineno
%option nounput
%option noinput
%option extra-type="c0::Driver*"

%x IN_COMMENT

%%

    /* --- Whitespace & Single-line Comments --- */
[ \t\v\f\r]+            { /* ignore whitespace */ }
\n                      { yyextra->column = 1; }
"//"[^\n]*              { /* ignore single-line comment */ }

    /* --- Delimited Nested Comments State Machine --- */
"/*"                    {
                            yyextra->comment_depth = 1;
                            yyextra->comment_start_loc = {yylineno, yylloc->first_column};
                            BEGIN(IN_COMMENT);
                        }
<IN_COMMENT>{
    "/*"                { yyextra->comment_depth++; }
    "*/"                {
                            if (--yyextra->comment_depth == 0) {
                                BEGIN(INITIAL);
                            }
                        }
    \n                  { yyextra->column = 1; }
    [^*\n/]+            { /* ignore comment body */ }
    "*"                 { /* ignore lone asterisk */ }
    "/"                 { /* ignore lone slash */ }
    <<EOF>>             {
                            yyextra->report_error(yyextra->comment_start_loc, "Unterminated delimited comment");
                            BEGIN(INITIAL);
                            return 0;
                        }
}
"*/"                    {
                            yyextra->report_error({yylineno, yylloc->first_column}, "Unmatched closing comment '*/'");
                        }

    /* --- Compiler Directives (#use) --- */
"#use"[ \t]*"<"([^>\n]+)">"[ \t]*       {
                                            std::string text(yytext, yyleng);
                                            size_t start = text.find('<') + 1;
                                            size_t end = text.find('>');
                                            std::string lib = text.substr(start, end - start);
                                            yylval->str_val = &yyextra->intern_string(std::move(lib));
                                            return TOKEN_USE_LIB;
                                        }
"#use"[ \t]*\"([^"\n]+)\"[ \t]*         {
                                            std::string text(yytext, yyleng);
                                            size_t start = text.find('"') + 1;
                                            size_t end = text.rfind('"');
                                            std::string str = text.substr(start, end - start);
                                            yylval->str_val = &yyextra->intern_string(std::move(str));
                                            return TOKEN_USE_STR;
                                        }
"#"[^\n]*                               { /* ignore other preprocessor directives */ }

    /* --- Reserved Keywords --- */
"int"                   { return KW_INT; }
"bool"                  { return KW_BOOL; }
"string"                { return KW_STRING; }
"char"                  { return KW_CHAR; }
"void"                  { return KW_VOID; }
"struct"                { return KW_STRUCT; }
"typedef"               { return KW_TYPEDEF; }
"if"                    { return KW_IF; }
"else"                  { return KW_ELSE; }
"while"                 { return KW_WHILE; }
"for"                   { return KW_FOR; }
"continue"              { return KW_CONTINUE; }
"break"                 { return KW_BREAK; }
"return"                { return KW_RETURN; }
"assert"                { return KW_ASSERT; }
"error"                 { return KW_ERROR; }
"true"                  { return KW_TRUE; }
"false"                 { return KW_FALSE; }
"NULL"                  { return KW_NULL; }
"alloc"                 { return KW_ALLOC; }
"alloc_array"           { return KW_ALLOC_ARRAY; }

    /* --- Numerical Constants --- */
0[xX][0-9a-fA-F]+       {
                            errno = 0;
                            char* endptr = nullptr;
                            unsigned long long val = std::strtoull(yytext, &endptr, 16);
                            if (errno == ERANGE || val > 0xFFFFFFFFULL) {
                                yyextra->report_error({yylineno, yylloc->first_column}, "Hexadecimal constant out of range (must fit in 32 bits)");
                                yylval->int_val = 0;
                            } else {
                                yylval->int_val = static_cast<int32_t>(static_cast<uint32_t>(val));
                            }
                            return TOKEN_INT_LIT;
                        }

0|[1-9][0-9]*           {
                            errno = 0;
                            char* endptr = nullptr;
                            unsigned long long val = std::strtoull(yytext, &endptr, 10);
                            // In C0, decimal range is 0 to 2^31 (2147483648)
                            if (errno == ERANGE || val > 2147483648ULL) {
                                yyextra->report_error({yylineno, yylloc->first_column}, "Integer constant out of range (decimal constant must not exceed 2147483648)");
                                yylval->int_val = 0;
                            } else {
                                yylval->int_val = static_cast<int32_t>(static_cast<uint32_t>(val));
                            }
                            return TOKEN_INT_LIT;
                        }

    /* --- String Literals --- */
\"([^"\\\n]|\\.)*\"     {
                            std::string out, err;
                            if (!parse_c0_string(yytext, yyleng, out, err)) {
                                yyextra->report_error({yylineno, yylloc->first_column}, err);
                                yylval->str_val = &yyextra->intern_string("");
                            } else {
                                yylval->str_val = &yyextra->intern_string(std::move(out));
                            }
                            return TOKEN_STR_LIT;
                        }

\"([^"\\\n]|\\.)*       {
                            yyextra->report_error({yylineno, yylloc->first_column}, "Unterminated string literal");
                            yylval->str_val = &yyextra->intern_string("");
                            return TOKEN_STR_LIT;
                        }

    /* --- Character Literals --- */
'([^'\\\n]|\\.)+'       {
                            char c = 0;
                            std::string err;
                            if (!parse_c0_char(yytext, yyleng, c, err)) {
                                yyextra->report_error({yylineno, yylloc->first_column}, err);
                                yylval->char_val = 0;
                            } else {
                                yylval->char_val = c;
                            }
                            return TOKEN_CHAR_LIT;
                        }

'([^'\\\n]|\\.)*        {
                            yyextra->report_error({yylineno, yylloc->first_column}, "Unterminated character literal");
                            yylval->char_val = 0;
                            return TOKEN_CHAR_LIT;
                        }

    /* --- Compound Operators (Matched before single-char ops) --- */
"->"                    { return ARROW; }
"=="                    { return EQ_OP; }
"!="                    { return NE_OP; }
"<="                    { return LE_OP; }
">="                    { return GE_OP; }
"&&"                    { return AND_OP; }
"||"                    { return OR_OP; }
"<<"                    { return SHL_OP; }
">>"                    { return SHR_OP; }
"++"                    { return INC_OP; }
"--"                    { return DEC_OP; }
"+="                    { return ADD_ASSIGN; }
"-="                    { return SUB_ASSIGN; }
"*="                    { return MUL_ASSIGN; }
"/="                    { return DIV_ASSIGN; }
"%="                    { return MOD_ASSIGN; }
"&="                    { return AND_ASSIGN; }
"^="                    { return XOR_ASSIGN; }
"|="                    { return OR_ASSIGN; }
"<<="                   { return SHL_ASSIGN; }
">>="                   { return SHR_ASSIGN; }

    /* --- Single-character Operators & Punctuation --- */
"+"                     { return '+'; }
"-"                     { return '-'; }
"*"                     { return '*'; }
"/"                     { return '/'; }
"%"                     { return '%'; }
"<"                     { return '<'; }
">"                     { return '>'; }
"&"                     { return '&'; }
"^"                     { return '^'; }
"|"                     { return '|'; }
"~"                     { return '~'; }
"!"                     { return '!'; }
"?"                     { return '?'; }
":"                     { return ':'; }
"."                     { return '.'; }
"="                     { return '='; }
"("                     { return '('; }
")"                     { return ')'; }
"["                     { return '['; }
"]"                     { return ']'; }
"{"                     { return '{'; }
"}"                     { return '}'; }
","                     { return ','; }
";"                     { return ';'; }

    /* --- Identifiers & Type Identifiers --- */
[A-Za-z_][A-Za-z0-9_]*  {
                            yylval->str_val = &yyextra->intern_string(yytext);
                            if (yyextra->is_type_name(yytext)) {
                                return TYPE_IDENTIFIER;
                            }
                            return IDENTIFIER;
                        }

    /* --- Illegal Characters --- */
.                       {
                            yyextra->report_error({yylineno, yylloc->first_column}, std::string("Unexpected character: '") + yytext + "'");
                        }

%%