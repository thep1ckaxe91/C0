%code requires {
#include <cstdint>
#include <string>
#include <vector>
#include "c0/ast.h"

namespace c0 {
    class Driver;
}

typedef void* yyscan_t;

struct Prototype {
    c0::Type* return_type;
    std::string name;
    std::vector<c0::ParamDecl*> params;
    c0::SourceRange range;
};

}

%code top {
#include "c0/ast.h"
#include "c0/driver.h"
#include "parser.tab.hpp"

int yylex(YYSTYPE* yylval_param, YYLTYPE* yylloc_param, yyscan_t yyscanner);
void yyerror(YYLTYPE* locp, yyscan_t scanner, c0::Driver* driver, const char* msg);

inline c0::SourceRange make_range(const YYLTYPE& loc) {
    return c0::SourceRange{{loc.first_line, loc.first_column}, {loc.last_line, loc.last_column}};
}

}

%define api.pure full
%locations
%lex-param { yyscan_t scanner }
%parse-param { yyscan_t scanner }
%parse-param { c0::Driver* driver }

%union {
    int32_t int_val;
    char char_val;
    const std::string* str_val;
    c0::Type* type_val;
    c0::Exp* exp_val;
    c0::Stmt* stmt_val;
    c0::TopDecl* top_decl_val;
    c0::ParamDecl* param_val;
    c0::FieldDecl* field_val;
    std::vector<c0::Exp*>* exp_list_val;
    std::vector<c0::Stmt*>* stmt_list_val;
    std::vector<c0::TopDecl*>* top_list_val;
    std::vector<c0::ParamDecl*>* param_list_val;
    std::vector<c0::FieldDecl*>* field_list_val;
    c0::AssignOp asnop_val;
    Prototype* proto_val;
}

/* Reserved keywords */
%token KW_INT "int"
%token KW_BOOL "bool"
%token KW_STRING "string"
%token KW_CHAR "char"
%token KW_VOID "void"
%token KW_STRUCT "struct"
%token KW_TYPEDEF "typedef"
%token KW_IF "if"
%token KW_ELSE "else"
%token KW_WHILE "while"
%token KW_FOR "for"
%token KW_CONTINUE "continue"
%token KW_BREAK "break"
%token KW_RETURN "return"
%token KW_ASSERT "assert"
%token KW_ERROR "error"
%token KW_TRUE "true"
%token KW_FALSE "false"
%token KW_NULL "NULL"
%token KW_ALLOC "alloc"
%token KW_ALLOC_ARRAY "alloc_array"

/* Literals and Identifiers */
%token <int_val> TOKEN_INT_LIT
%token <char_val> TOKEN_CHAR_LIT
%token <str_val> TOKEN_STR_LIT
%token <str_val> IDENTIFIER
%token <str_val> TYPE_IDENTIFIER
%token <str_val> TOKEN_USE_LIB
%token <str_val> TOKEN_USE_STR

/* Compound operators */
%token ARROW "->"
%token EQ_OP "=="
%token NE_OP "!="
%token LE_OP "<="
%token GE_OP ">="
%token AND_OP "&&"
%token OR_OP "||"
%token SHL_OP "<<"
%token SHR_OP ">>"
%token INC_OP "++"
%token DEC_OP "--"
%token ADD_ASSIGN "+="
%token SUB_ASSIGN "-="
%token MUL_ASSIGN "*="
%token DIV_ASSIGN "/="
%token MOD_ASSIGN "%="
%token AND_ASSIGN "&="
%token XOR_ASSIGN "^="
%token OR_ASSIGN "|="
%token SHL_ASSIGN "<<="
%token SHR_ASSIGN ">>="

/* Precedence declarations (Figure 3 in C0 reference, lowest to highest) */
%precedence THEN
%precedence KW_ELSE

%right '?' ':'
%left OR_OP
%left AND_OP
%left '|'
%left '^'
%left '&'
%left EQ_OP NE_OP
%left '<' LE_OP '>' GE_OP
%left SHL_OP SHR_OP
%left '+' '-'
%left '*' '/' '%'
%right UNARY
%left ARROW '.' '[' ']' '(' ')'

/* Non-terminal types */
%type <top_list_val> program top_decl_list
%type <top_decl_val> top_decl gdecl gdefn
%type <proto_val> fn_prototype
%type <param_list_val> opt_param_list param_list
%type <param_val> param
%type <field_list_val> field_list
%type <field_val> field
%type <type_val> type base_type
%type <stmt_list_val> stmt_list
%type <stmt_val> stmt simple opt_simple
%type <asnop_val> asnop
%type <exp_val> exp opt_exp
%type <exp_list_val> opt_arg_list arg_list
%type <str_val> any_ident

%start program

%%

program:
    top_decl_list
    {
        $$ = $1;
        driver->set_ast(driver->make_node<c0::Program>(std::move(*$1), make_range(@$)));
    }
;

top_decl_list:
    /* empty */
    {
        $$ = driver->make_vector<c0::TopDecl*>();
    }
  | top_decl_list top_decl
    {
        $$ = $1;
        if ($2) {
            $$->push_back($2);
        }
    }
;

top_decl:
    gdecl { $$ = $1; }
  | gdefn { $$ = $1; }
  | error ';'
    {
        yyerrok;
        $$ = nullptr;
    }
  | error '}'
    {
        yyerrok;
        $$ = nullptr;
    }
;

gdecl:
    KW_STRUCT any_ident ';'
    {
        $$ = driver->make_node<c0::StructDecl>(*$2, make_range(@$));
    }
  | fn_prototype ';'
    {
        $$ = driver->make_node<c0::FnDecl>($1->return_type, $1->name, std::move($1->params), make_range(@$));
    }
  | TOKEN_USE_LIB
    {
        $$ = driver->make_node<c0::UseDirective>(*$1, true, make_range(@$));
    }
  | TOKEN_USE_STR
    {
        $$ = driver->make_node<c0::UseDirective>(*$1, false, make_range(@$));
    }
;

gdefn:
    KW_STRUCT any_ident '{' field_list '}' ';'
    {
        $$ = driver->make_node<c0::StructDef>(*$2, std::move(*$4), make_range(@$));
    }
  | fn_prototype '{' stmt_list '}'
    {
        auto* body = driver->make_node<c0::BlockStmt>(std::move(*$3), make_range(@3));
        $$ = driver->make_node<c0::FnDef>($1->return_type, $1->name, std::move($1->params), body, make_range(@$));
    }
  | KW_TYPEDEF type any_ident ';'
    {
        driver->add_type_name(*$3);
        $$ = driver->make_node<c0::TypeDef>($2, *$3, make_range(@$));
    }
  | KW_TYPEDEF fn_prototype ';'
    {
        driver->add_type_name($2->name);
        $$ = driver->make_node<c0::FnTypeDef>($2->return_type, $2->name, std::move($2->params), make_range(@$));
    }
;

fn_prototype:
    type IDENTIFIER '(' opt_param_list ')'
    {
        auto* proto = new Prototype{$1, *$2, std::move(*$4), make_range(@$)};
        $$ = proto;
    }
;

opt_param_list:
    /* empty */
    {
        $$ = driver->make_vector<c0::ParamDecl*>();
    }
  | param_list
    {
        $$ = $1;
    }
;

param_list:
    param
    {
        $$ = driver->make_vector<c0::ParamDecl*>();
        $$->push_back($1);
    }
  | param_list ',' param
    {
        $$ = $1;
        $$->push_back($3);
    }
;

param:
    type IDENTIFIER
    {
        $$ = driver->make_node<c0::ParamDecl>($1, *$2, make_range(@$));
    }
;

field_list:
    /* empty */
    {
        $$ = driver->make_vector<c0::FieldDecl*>();
    }
  | field_list field
    {
        $$ = $1;
        if ($2) {
            $$->push_back($2);
        }
    }
;

field:
    type any_ident ';'
    {
        $$ = driver->make_node<c0::FieldDecl>($1, *$2, make_range(@$));
    }
  | error ';'
    {
        yyerrok;
        $$ = nullptr;
    }
;

type:
    base_type
    {
        $$ = $1;
    }
  | type '*'
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Pointer, $1, make_range(@$));
    }
  | type '[' ']'
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Array, $1, make_range(@$));
    }
;

base_type:
    KW_INT
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Int, make_range(@$));
    }
  | KW_BOOL
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Bool, make_range(@$));
    }
  | KW_CHAR
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Char, make_range(@$));
    }
  | KW_STRING
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::String, make_range(@$));
    }
  | KW_VOID
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Void, make_range(@$));
    }
  | KW_STRUCT any_ident
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Struct, *$2, make_range(@$));
    }
  | TYPE_IDENTIFIER
    {
        $$ = driver->make_node<c0::Type>(c0::TypeKind::Alias, *$1, make_range(@$));
    }
;

stmt_list:
    /* empty */
    {
        $$ = driver->make_vector<c0::Stmt*>();
    }
  | stmt_list stmt
    {
        $$ = $1;
        if ($2) {
            $$->push_back($2);
        }
    }
;

stmt:
    simple ';'
    {
        $$ = $1;
    }
  | KW_IF '(' exp ')' stmt %prec THEN
    {
        $$ = driver->make_node<c0::IfStmt>($3, $5, nullptr, make_range(@$));
    }
  | KW_IF '(' exp ')' stmt KW_ELSE stmt
    {
        $$ = driver->make_node<c0::IfStmt>($3, $5, $7, make_range(@$));
    }
  | KW_WHILE '(' exp ')' stmt
    {
        $$ = driver->make_node<c0::WhileStmt>($3, $5, make_range(@$));
    }
  | KW_FOR '(' opt_simple ';' exp ';' opt_simple ')' stmt
    {
        $$ = driver->make_node<c0::ForStmt>($3, $5, $7, $9, make_range(@$));
    }
  | KW_RETURN opt_exp ';'
    {
        $$ = driver->make_node<c0::ReturnStmt>($2, make_range(@$));
    }
  | '{' stmt_list '}'
    {
        $$ = driver->make_node<c0::BlockStmt>(std::move(*$2), make_range(@$));
    }
  | KW_ASSERT '(' exp ')' ';'
    {
        $$ = driver->make_node<c0::AssertStmt>($3, make_range(@$));
    }
  | KW_ERROR '(' exp ')' ';'
    {
        $$ = driver->make_node<c0::ErrorStmt>($3, make_range(@$));
    }
  | KW_BREAK ';'
    {
        $$ = driver->make_node<c0::BreakStmt>(make_range(@$));
    }
  | KW_CONTINUE ';'
    {
        $$ = driver->make_node<c0::ContinueStmt>(make_range(@$));
    }
  | ';'
    {
        $$ = nullptr;
    }
  | error ';'
    {
        yyerrok;
        $$ = nullptr;
    }
  | error '}'
    {
        yyerrok;
        $$ = nullptr;
    }
;

opt_simple:
    /* empty */
    {
        $$ = nullptr;
    }
  | simple
    {
        $$ = $1;
    }
;

simple:
    exp
    {
        $$ = driver->make_node<c0::ExprStmt>($1, make_range(@$));
    }
  | exp asnop exp
    {
        $$ = driver->make_node<c0::AssignStmt>($1, $2, $3, make_range(@$));
    }
  | exp INC_OP
    {
        $$ = driver->make_node<c0::IncDecStmt>($1, true, make_range(@$));
    }
  | exp DEC_OP
    {
        $$ = driver->make_node<c0::IncDecStmt>($1, false, make_range(@$));
    }
  | type IDENTIFIER
    {
        $$ = driver->make_node<c0::VarDeclStmt>($1, *$2, nullptr, make_range(@$));
    }
  | type IDENTIFIER '=' exp
    {
        $$ = driver->make_node<c0::VarDeclStmt>($1, *$2, $4, make_range(@$));
    }
;

asnop:
    '='         { $$ = c0::AssignOp::ASSIGN; }
  | ADD_ASSIGN  { $$ = c0::AssignOp::ADD_ASSIGN; }
  | SUB_ASSIGN  { $$ = c0::AssignOp::SUB_ASSIGN; }
  | MUL_ASSIGN  { $$ = c0::AssignOp::MUL_ASSIGN; }
  | DIV_ASSIGN  { $$ = c0::AssignOp::DIV_ASSIGN; }
  | MOD_ASSIGN  { $$ = c0::AssignOp::MOD_ASSIGN; }
  | AND_ASSIGN  { $$ = c0::AssignOp::AND_ASSIGN; }
  | XOR_ASSIGN  { $$ = c0::AssignOp::XOR_ASSIGN; }
  | OR_ASSIGN   { $$ = c0::AssignOp::OR_ASSIGN; }
  | SHL_ASSIGN  { $$ = c0::AssignOp::SHL_ASSIGN; }
  | SHR_ASSIGN  { $$ = c0::AssignOp::SHR_ASSIGN; }
;

opt_exp:
    /* empty */
    {
        $$ = nullptr;
    }
  | exp
    {
        $$ = $1;
    }
;

exp:
    '(' exp ')'
    {
        $$ = $2;
    }
  | TOKEN_INT_LIT
    {
        $$ = driver->make_node<c0::IntLitExp>($1, make_range(@$));
    }
  | TOKEN_CHAR_LIT
    {
        $$ = driver->make_node<c0::CharLitExp>($1, make_range(@$));
    }
  | TOKEN_STR_LIT
    {
        $$ = driver->make_node<c0::StringLitExp>(*$1, make_range(@$));
    }
  | KW_TRUE
    {
        $$ = driver->make_node<c0::BoolLitExp>(true, make_range(@$));
    }
  | KW_FALSE
    {
        $$ = driver->make_node<c0::BoolLitExp>(false, make_range(@$));
    }
  | KW_NULL
    {
        $$ = driver->make_node<c0::NullLitExp>(make_range(@$));
    }
  | IDENTIFIER
    {
        $$ = driver->make_node<c0::VarExp>(*$1, make_range(@$));
    }
  | exp '+' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::ADD, $1, $3, make_range(@$));
    }
  | exp '-' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::SUB, $1, $3, make_range(@$));
    }
  | exp '*' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::MUL, $1, $3, make_range(@$));
    }
  | exp '/' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::DIV, $1, $3, make_range(@$));
    }
  | exp '%' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::MOD, $1, $3, make_range(@$));
    }
  | exp SHL_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::SHL, $1, $3, make_range(@$));
    }
  | exp SHR_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::SHR, $1, $3, make_range(@$));
    }
  | exp '<' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::LT, $1, $3, make_range(@$));
    }
  | exp LE_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::LE, $1, $3, make_range(@$));
    }
  | exp '>' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::GT, $1, $3, make_range(@$));
    }
  | exp GE_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::GE, $1, $3, make_range(@$));
    }
  | exp EQ_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::EQ, $1, $3, make_range(@$));
    }
  | exp NE_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::NE, $1, $3, make_range(@$));
    }
  | exp '&' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::BIT_AND, $1, $3, make_range(@$));
    }
  | exp '^' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::BIT_XOR, $1, $3, make_range(@$));
    }
  | exp '|' exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::BIT_OR, $1, $3, make_range(@$));
    }
  | exp AND_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::LOG_AND, $1, $3, make_range(@$));
    }
  | exp OR_OP exp
    {
        $$ = driver->make_node<c0::BinaryExp>(c0::BinaryOp::LOG_OR, $1, $3, make_range(@$));
    }
  | '!' exp %prec UNARY
    {
        $$ = driver->make_node<c0::UnaryExp>(c0::UnaryOp::LOG_NOT, $2, make_range(@$));
    }
  | '~' exp %prec UNARY
    {
        $$ = driver->make_node<c0::UnaryExp>(c0::UnaryOp::BIT_NOT, $2, make_range(@$));
    }
  | '-' exp %prec UNARY
    {
        $$ = driver->make_node<c0::UnaryExp>(c0::UnaryOp::NEG, $2, make_range(@$));
    }
  | '*' exp %prec UNARY
    {
        $$ = driver->make_node<c0::UnaryExp>(c0::UnaryOp::DEREF, $2, make_range(@$));
    }
  | '&' exp %prec UNARY
    {
        $$ = driver->make_node<c0::UnaryExp>(c0::UnaryOp::ADDR_OF, $2, make_range(@$));
    }
  | exp '?' exp ':' exp
    {
        $$ = driver->make_node<c0::TernaryExp>($1, $3, $5, make_range(@$));
    }
  | exp '(' opt_arg_list ')'
    {
        $$ = driver->make_node<c0::CallExp>($1, std::move(*$3), make_range(@$));
    }
  | exp '.' any_ident
    {
        $$ = driver->make_node<c0::FieldExp>($1, *$3, false, make_range(@$));
    }
  | exp ARROW any_ident
    {
        $$ = driver->make_node<c0::FieldExp>($1, *$3, true, make_range(@$));
    }
  | exp '[' exp ']'
    {
        $$ = driver->make_node<c0::IndexExp>($1, $3, make_range(@$));
    }
  | KW_ALLOC '(' type ')'
    {
        $$ = driver->make_node<c0::AllocExp>($3, make_range(@$));
    }
  | KW_ALLOC_ARRAY '(' type ',' exp ')'
    {
        $$ = driver->make_node<c0::AllocArrayExp>($3, $5, make_range(@$));
    }
  | '[' ']' '(' opt_param_list ')' ARROW type '{' stmt_list '}'
    {
        auto* body = driver->make_node<c0::BlockStmt>(std::move(*$9), make_range(@9));
        $$ = driver->make_node<c0::LambdaExp>($7, std::move(*$4), body, make_range(@$));
    }
;

opt_arg_list:
    /* empty */
    {
        $$ = driver->make_vector<c0::Exp*>();
    }
  | arg_list
    {
        $$ = $1;
    }
;

arg_list:
    exp
    {
        $$ = driver->make_vector<c0::Exp*>();
        $$->push_back($1);
    }
  | arg_list ',' exp
    {
        $$ = $1;
        $$->push_back($3);
    }
;

any_ident:
    IDENTIFIER      { $$ = $1; }
  | TYPE_IDENTIFIER { $$ = $1; }
;

%%

void yyerror(YYLTYPE* locp, yyscan_t scanner, c0::Driver* driver, const char* msg) {
    (void)scanner;
    driver->report_error(locp->first_line, locp->first_column, msg);
}
