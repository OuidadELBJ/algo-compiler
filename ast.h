#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include "token.h"

// AST Types

typedef enum {
    AST_PROGRAM,

    // Declarations
    AST_DECL_VAR,
    AST_DECL_CONST,
    AST_DECL_ARRAY,
   


    // Type
    AST_TYPE_ARRAY,   // tableau <elem_type> dims (optionnelles)
    AST_TYPE_PRIMITIVE,   // entier, reel, caractere, chaine, booleen
    AST_TYPE_NAMED,       // type user-defined (structure)
    // For arrays, dims are stored in AST_DECL_ARRAY

    // Definitions
    AST_DEF_STRUCT,
    AST_DEF_FUNC,
    AST_DEF_PROC,
    AST_PARAM,
    AST_FIELD,

    // Statements / Blocks
    AST_BLOCK,
    AST_ASSIGN,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_REPEAT,
    AST_CALL_STMT,
    AST_RETURN,
    AST_WRITE,
    AST_READ,
    AST_BREAK,       // Sortir
    AST_QUIT_FOR,    // Quitter Pour

    // Switch (Selon)
    AST_SWITCH,
    AST_CASE,

    // Expressions
    AST_BINARY,
    AST_UNARY,
    AST_LITERAL_INT,
    AST_LITERAL_REAL,
    AST_LITERAL_STRING,
    AST_LITERAL_BOOL,
    AST_IDENT,

    // Postfix
    AST_INDEX,   // base[index]
    AST_FIELD_ACCESS, // base.field
    AST_CALL     // callee(args)
} ASTKind;

typedef enum {
    TYPE_ENTIER,
    TYPE_REEL,
    TYPE_CARACTERE,
    TYPE_CHAINE,
    TYPE_BOOLEEN
} PrimitiveType;

typedef struct ASTNode ASTNode;

// Simple list container
typedef struct {
    ASTNode** items;
    int count;
    int cap;
} ASTList;

struct ASTNode {
    ASTKind kind;

    // Source position (best effort)
    int line;
    int col;

    union {
        // PROGRAM: name + decls + defs + main_block
        struct {
            char* name;
            ASTList decls;   // AST_DECL_*
            ASTList defs;    // AST_DEF_*
            ASTNode* main_block; // AST_BLOCK
        } program;

        // DECL_VAR: name + type
        struct {
            char* name;
            ASTNode* type; // AST_TYPE_*
        } decl_var;

        // DECL_CONST: name + type + value (expr)
        struct {
            char* name;
            ASTNode* type;   // AST_TYPE_*
            ASTNode* value;  // expression
        } decl_const;

        // DECL_ARRAY: name + elem_type + dims (expressions)
        struct {
            char* name;
            ASTNode* elem_type; // AST_TYPE_*
            ASTList dims;       // expressions (usually const)
        } decl_array;

        // TYPE_PRIMITIVE
        struct {
            PrimitiveType prim;
        } type_prim;

        // TYPE_NAMED
        struct {
            char* name;
        } type_named;

        // STRUCT: name + fields (AST_FIELD)
        struct {
            char* name;
            ASTList fields; // AST_FIELD
        } def_struct;

        // FUNC: name + params + return_type + body
        struct {
            char* name;
            ASTList params;      // AST_PARAM
            ASTNode* return_type; // AST_TYPE_*
            ASTNode* body;       // AST_BLOCK
        } def_func;

        // PROC: name + params + body
        struct {
            char* name;
            ASTList params; // AST_PARAM
            ASTNode* body;  // AST_BLOCK
        } def_proc;

        // PARAM: name + type
        struct {
            char* name;
            ASTNode* type; // AST_TYPE_*
        } param;

        // TYPE_ARRAY: elem_type + dims (NULL pour [])
        struct {
             ASTNode* elem_type; // AST_TYPE_*
             ASTList dims;       // expressions ou NULL pour "[]"
        } type_array;


        // FIELD: name + type
        struct {
            char* name;
            ASTNode* type; // AST_TYPE_*
        } field;

        // BLOCK: statements
        struct {
            ASTList stmts; // statements
        } block;

        // ASSIGN: target (lvalue expr) + value expr
        struct {
            ASTNode* target;
            ASTNode* value;
        } assign;

        // IF: cond + then_block + elseif list (pairs) + else_block?
        struct {
            ASTNode* cond;
            ASTNode* then_block; // AST_BLOCK
            ASTList elif_conds;  // expressions
            ASTList elif_blocks; // blocks
            ASTNode* else_block; // AST_BLOCK or NULL
        } if_stmt;

        // WHILE: cond + body
        struct {
            ASTNode* cond;
            ASTNode* body; // AST_BLOCK
        } while_stmt;

        // FOR: var name + start expr + end expr + step expr? + body
        struct {
            char* var;
            ASTNode* start;
            ASTNode* end;
            ASTNode* step; // may be NULL
            ASTNode* body; // AST_BLOCK
        } for_stmt;

        // REPEAT: body + until_cond (optional)
        struct {
            ASTNode* body;       // AST_BLOCK
            ASTNode* until_cond; // expression (may be NULL depending on your syntax choice)
        } repeat_stmt;

        // CALL_STMT: call expression (AST_CALL)
        struct {
            ASTNode* call;
        } call_stmt;

        // RETURN: value or NULL
        struct {
            ASTNode* value;
        } ret_stmt;

        // WRITE: args
        struct {
            ASTList args; // expressions
        } write_stmt;

        // READ: targets
        struct {
            ASTList targets; // lvalues
        } read_stmt;

        // SWITCH: expr + cases + default_block
        struct {
            ASTNode* expr;
            ASTList cases;       // AST_CASE
            ASTNode* default_block; // AST_BLOCK or NULL
        } switch_stmt;

        // CASE: values + block
        struct {
            ASTList values; // expressions (usually constants)
            ASTNode* body;  // AST_BLOCK
        } case_stmt;

        // BINARY: op + lhs + rhs
        struct {
            TokenType op;
            ASTNode* lhs;
            ASTNode* rhs;
        } binary;

        // UNARY: op + expr
        struct {
            TokenType op; // TOK_NON or TOK_MOINS
            ASTNode* expr;
        } unary;

        // LITERALS
        struct { long long value; } lit_int;
        struct { char* text; } lit_real;     // keep lexeme "1,5" or "1.5"
        struct { char* text; } lit_string;
        struct { bool value; } lit_bool;

        // IDENT
        struct { char* name; } ident;

        // INDEX: base + index
        struct {
            ASTNode* base;
            ASTNode* index;
        } index;

        // FIELD_ACCESS: base + field
        struct {
            ASTNode* base;
            char* field;
        } field_access;

        // CALL: callee + args
        struct {
            ASTNode* callee;
            ASTList args;
        } call;
    } as;
};

// =====================
// List helpers
// =====================
void ast_list_init(ASTList* list);
void ast_list_push(ASTList* list, ASTNode* node);
void ast_list_free_shallow(ASTList* list);

// =====================
// Node constructors
// =====================
ASTNode* ast_new_program(const char* name, int line, int col);
ASTNode* ast_new_block(int line, int col);

ASTNode* ast_new_type_primitive(PrimitiveType prim, int line, int col);
ASTNode* ast_new_type_named(const char* name, int line, int col);
ASTNode* ast_new_type_array(ASTNode* elem_type, int line, int col);


ASTNode* ast_new_decl_var(const char* name, ASTNode* type, int line, int col);
ASTNode* ast_new_decl_const(const char* name, ASTNode* type, ASTNode* value, int line, int col);
ASTNode* ast_new_decl_array(const char* name, ASTNode* elem_type, int line, int col);

ASTNode* ast_new_def_struct(const char* name, int line, int col);
ASTNode* ast_new_field(const char* name, ASTNode* type, int line, int col);

ASTNode* ast_new_def_func(const char* name, ASTNode* return_type, int line, int col);
ASTNode* ast_new_def_proc(const char* name, int line, int col);
ASTNode* ast_new_param(const char* name, ASTNode* type, int line, int col);

ASTNode* ast_new_assign(ASTNode* target, ASTNode* value, int line, int col);
ASTNode* ast_new_if(ASTNode* cond, ASTNode* then_block, int line, int col);
ASTNode* ast_new_while(ASTNode* cond, ASTNode* body, int line, int col);
ASTNode* ast_new_for(const char* var, ASTNode* start, ASTNode* end, ASTNode* step, ASTNode* body, int line, int col);
ASTNode* ast_new_repeat(ASTNode* body, ASTNode* until_cond, int line, int col);

ASTNode* ast_new_write(int line, int col);
ASTNode* ast_new_read(int line, int col);
ASTNode* ast_new_return(ASTNode* value, int line, int col);
ASTNode* ast_new_call_stmt(ASTNode* call_expr, int line, int col);
ASTNode* ast_new_break(int line, int col);
ASTNode* ast_new_quit_for(int line, int col);

ASTNode* ast_new_switch(ASTNode* expr, int line, int col);
ASTNode* ast_new_case(int line, int col);

ASTNode* ast_new_binary(TokenType op, ASTNode* lhs, ASTNode* rhs, int line, int col);
ASTNode* ast_new_unary(TokenType op, ASTNode* expr, int line, int col);

ASTNode* ast_new_lit_int(long long v, int line, int col);
ASTNode* ast_new_lit_real(const char* text, int line, int col);
ASTNode* ast_new_lit_string(const char* text, int line, int col);
ASTNode* ast_new_lit_bool(bool v, int line, int col);

ASTNode* ast_new_ident(const char* name, int line, int col);

ASTNode* ast_new_index(ASTNode* base, ASTNode* index, int line, int col);
ASTNode* ast_new_field_access(ASTNode* base, const char* field, int line, int col);
ASTNode* ast_new_call(ASTNode* callee, int line, int col);

// =====================
// Utilities
// =====================
void ast_block_add(ASTNode* block, ASTNode* stmt);
void ast_program_add_decl(ASTNode* program, ASTNode* decl);
void ast_program_add_def(ASTNode* program, ASTNode* def);

void ast_free(ASTNode* node);

// Optional pretty print
void ast_print(ASTNode* node);

#endif
