#ifndef SEMANTIQUE_H
#define SEMANTIQUE_H

#include <stdbool.h>
#include "ast.h"


typedef enum {
    TY_ERROR = 0,
    TY_VOID,
    TY_INT,
    TY_REAL,
    TY_BOOL,
    TY_CHAR,
    TY_STRING,
    TY_ARRAY,
    TY_STRUCT
} TypeKind;
//commentaire
typedef struct Type Type;

typedef struct {
    Type* elem;
    int dims; // remaining dimensions
} ArrayInfo;

typedef struct {
    char* name; // struct name
} StructInfo;

struct Type {
    TypeKind kind;
    union {
        ArrayInfo array;
        StructInfo st;
    } as;
};


typedef enum {
    SYM_VAR,
    SYM_CONST,
    SYM_ARRAY,
    SYM_STRUCT,
    SYM_FUNC,
    SYM_PROC,
    SYM_PARAM
} SymbolKind;

typedef struct Symbol {
    char* name;
    SymbolKind kind;
    Type* type;

    // for const int evaluation (useful for dims/case labels)
    bool has_int_value;
    long long int_value;

    // functions/procs
    int param_count;
    Type** param_types;
    char** param_names;   // optional
    Type* return_type;    // func only (proc => TY_VOID)
} Symbol;

typedef struct Scope {
    struct Scope* parent;
    Symbol* symbols;
    int count;
    int cap;
} Scope;

// =====================
// Semantic context
// =====================

typedef struct {
    Scope* scope;      // current scope (stack)
    char** errors;
    int err_count;
    int err_cap;

    // context to validate break/quit/return
    int loop_depth;
    int for_depth;
    int switch_depth;

    // current function return type (NULL => main or procedure/none)
    bool in_function;
    bool in_procedure;
    Type* current_return_type;
} SemContext;


void sem_init(SemContext* ctx);
void sem_free(SemContext* ctx);

bool sem_analyze_program(SemContext* ctx, ASTNode* program);

void sem_print_errors(SemContext* ctx);

#endif
