#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char* sdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* r = (char*)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

void ast_list_init(ASTList* list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void ast_list_push(ASTList* list, ASTNode* node) {
    if (!list) return;
    if (list->count >= list->cap) {
        int ncap = (list->cap == 0) ? 8 : list->cap * 2;
        ASTNode** nitems = (ASTNode**)realloc(list->items, (size_t)ncap * sizeof(ASTNode*));
        if (!nitems) return;
        list->items = nitems;
        list->cap = ncap;
    }
    list->items[list->count++] = node;
}

void ast_list_free_shallow(ASTList* list) {
    if (!list) return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static ASTNode* ast_alloc(ASTKind kind, int line, int col) {
    ASTNode* n = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!n) return NULL;
    n->kind = kind;
    n->line = line;
    n->col = col;
    return n;
}

ASTNode* ast_new_program(const char* name, int line, int col) {
    ASTNode* n = ast_alloc(AST_PROGRAM, line, col);
    n->as.program.name = sdup(name);
    ast_list_init(&n->as.program.decls);
    ast_list_init(&n->as.program.defs);
    n->as.program.main_block = NULL;
    return n;
}

ASTNode* ast_new_block(int line, int col) {
    ASTNode* n = ast_alloc(AST_BLOCK, line, col);
    ast_list_init(&n->as.block.stmts);
    return n;
}

ASTNode* ast_new_type_primitive(PrimitiveType prim, int line, int col) {
    ASTNode* n = ast_alloc(AST_TYPE_PRIMITIVE, line, col);
    n->as.type_prim.prim = prim;
    return n;
}

ASTNode* ast_new_type_named(const char* name, int line, int col) {
    ASTNode* n = ast_alloc(AST_TYPE_NAMED, line, col);
    n->as.type_named.name = sdup(name);
    return n;
}

ASTNode* ast_new_decl_var(const char* name, ASTNode* type, int line, int col) {
    ASTNode* n = ast_alloc(AST_DECL_VAR, line, col);
    n->as.decl_var.name = sdup(name);
    n->as.decl_var.type = type;
    return n;
}

ASTNode* ast_new_decl_const(const char* name, ASTNode* type, ASTNode* value, int line, int col) {
    ASTNode* n = ast_alloc(AST_DECL_CONST, line, col);
    n->as.decl_const.name = sdup(name);
    n->as.decl_const.type = type;
    n->as.decl_const.value = value;
    return n;
}

ASTNode* ast_new_decl_array(const char* name, ASTNode* elem_type, int line, int col) {
    ASTNode* n = ast_alloc(AST_DECL_ARRAY, line, col);
    n->as.decl_array.name = sdup(name);
    n->as.decl_array.elem_type = elem_type;
    ast_list_init(&n->as.decl_array.dims);
    return n;
}

ASTNode* ast_new_def_struct(const char* name, int line, int col) {
    ASTNode* n = ast_alloc(AST_DEF_STRUCT, line, col);
    n->as.def_struct.name = sdup(name);
    ast_list_init(&n->as.def_struct.fields);
    return n;
}

ASTNode* ast_new_field(const char* name, ASTNode* type, int line, int col) {
    ASTNode* n = ast_alloc(AST_FIELD, line, col);
    n->as.field.name = sdup(name);
    n->as.field.type = type;
    return n;
}

ASTNode* ast_new_def_func(const char* name, ASTNode* return_type, int line, int col) {
    ASTNode* n = ast_alloc(AST_DEF_FUNC, line, col);
    n->as.def_func.name = sdup(name);
    ast_list_init(&n->as.def_func.params);
    n->as.def_func.return_type = return_type;
    n->as.def_func.body = NULL;
    return n;
}

ASTNode* ast_new_def_proc(const char* name, int line, int col) {
    ASTNode* n = ast_alloc(AST_DEF_PROC, line, col);
    n->as.def_proc.name = sdup(name);
    ast_list_init(&n->as.def_proc.params);
    n->as.def_proc.body = NULL;
    return n;
}

ASTNode* ast_new_param(const char* name, ASTNode* type, int line, int col) {
    ASTNode* n = ast_alloc(AST_PARAM, line, col);
    n->as.param.name = sdup(name);
    n->as.param.type = type;
    return n;
}

ASTNode* ast_new_type_array(ASTNode* elem_type, int line, int col) {
    ASTNode* n = ast_alloc(AST_TYPE_ARRAY, line, col);
    n->as.type_array.elem_type = elem_type;
    ast_list_init(&n->as.type_array.dims);
    return n;
}


ASTNode* ast_new_assign(ASTNode* target, ASTNode* value, int line, int col) {
    ASTNode* n = ast_alloc(AST_ASSIGN, line, col);
    n->as.assign.target = target;
    n->as.assign.value = value;
    return n;
}

ASTNode* ast_new_if(ASTNode* cond, ASTNode* then_block, int line, int col) {
    ASTNode* n = ast_alloc(AST_IF, line, col);
    n->as.if_stmt.cond = cond;
    n->as.if_stmt.then_block = then_block;
    ast_list_init(&n->as.if_stmt.elif_conds);
    ast_list_init(&n->as.if_stmt.elif_blocks);
    n->as.if_stmt.else_block = NULL;
    return n;
}

ASTNode* ast_new_while(ASTNode* cond, ASTNode* body, int line, int col) {
    ASTNode* n = ast_alloc(AST_WHILE, line, col);
    n->as.while_stmt.cond = cond;
    n->as.while_stmt.body = body;
    return n;
}

ASTNode* ast_new_for(const char* var, ASTNode* start, ASTNode* end, ASTNode* step, ASTNode* body, int line, int col) {
    ASTNode* n = ast_alloc(AST_FOR, line, col);
    n->as.for_stmt.var = sdup(var);
    n->as.for_stmt.start = start;
    n->as.for_stmt.end = end;
    n->as.for_stmt.step = step;
    n->as.for_stmt.body = body;
    return n;
}

ASTNode* ast_new_repeat(ASTNode* body, ASTNode* until_cond, int line, int col) {
    ASTNode* n = ast_alloc(AST_REPEAT, line, col);
    n->as.repeat_stmt.body = body;
    n->as.repeat_stmt.until_cond = until_cond;
    return n;
}

ASTNode* ast_new_write(int line, int col) {
    ASTNode* n = ast_alloc(AST_WRITE, line, col);
    ast_list_init(&n->as.write_stmt.args);
    return n;
}

ASTNode* ast_new_read(int line, int col) {
    ASTNode* n = ast_alloc(AST_READ, line, col);
    ast_list_init(&n->as.read_stmt.targets);
    return n;
}

ASTNode* ast_new_return(ASTNode* value, int line, int col) {
    ASTNode* n = ast_alloc(AST_RETURN, line, col);
    n->as.ret_stmt.value = value;
    return n;
}

ASTNode* ast_new_call_stmt(ASTNode* call_expr, int line, int col) {
    ASTNode* n = ast_alloc(AST_CALL_STMT, line, col);
    n->as.call_stmt.call = call_expr;
    return n;
}

ASTNode* ast_new_break(int line, int col) {
    return ast_alloc(AST_BREAK, line, col);
}

ASTNode* ast_new_quit_for(int line, int col) {
    return ast_alloc(AST_QUIT_FOR, line, col);
}

ASTNode* ast_new_switch(ASTNode* expr, int line, int col) {
    ASTNode* n = ast_alloc(AST_SWITCH, line, col);
    n->as.switch_stmt.expr = expr;
    ast_list_init(&n->as.switch_stmt.cases);
    n->as.switch_stmt.default_block = NULL;
    return n;
}

ASTNode* ast_new_case(int line, int col) {
    ASTNode* n = ast_alloc(AST_CASE, line, col);
    ast_list_init(&n->as.case_stmt.values);
    n->as.case_stmt.body = NULL;
    return n;
}

ASTNode* ast_new_binary(TokenType op, ASTNode* lhs, ASTNode* rhs, int line, int col) {
    ASTNode* n = ast_alloc(AST_BINARY, line, col);
    n->as.binary.op = op;
    n->as.binary.lhs = lhs;
    n->as.binary.rhs = rhs;
    return n;
}

ASTNode* ast_new_unary(TokenType op, ASTNode* expr, int line, int col) {
    ASTNode* n = ast_alloc(AST_UNARY, line, col);
    n->as.unary.op = op;
    n->as.unary.expr = expr;
    return n;
}

ASTNode* ast_new_lit_int(long long v, int line, int col) {
    ASTNode* n = ast_alloc(AST_LITERAL_INT, line, col);
    n->as.lit_int.value = v;
    return n;
}

ASTNode* ast_new_lit_real(const char* text, int line, int col) {
    ASTNode* n = ast_alloc(AST_LITERAL_REAL, line, col);
    n->as.lit_real.text = sdup(text);
    return n;
}

ASTNode* ast_new_lit_string(const char* text, int line, int col) {
    ASTNode* n = ast_alloc(AST_LITERAL_STRING, line, col);
    n->as.lit_string.text = sdup(text);
    return n;
}

ASTNode* ast_new_lit_bool(bool v, int line, int col) {
    ASTNode* n = ast_alloc(AST_LITERAL_BOOL, line, col);
    n->as.lit_bool.value = v;
    return n;
}

ASTNode* ast_new_ident(const char* name, int line, int col) {
    ASTNode* n = ast_alloc(AST_IDENT, line, col);
    n->as.ident.name = sdup(name);
    return n;
}

ASTNode* ast_new_index(ASTNode* base, ASTNode* index, int line, int col) {
    ASTNode* n = ast_alloc(AST_INDEX, line, col);
    n->as.index.base = base;
    n->as.index.index = index;
    return n;
}

ASTNode* ast_new_field_access(ASTNode* base, const char* field, int line, int col) {
    ASTNode* n = ast_alloc(AST_FIELD_ACCESS, line, col);
    n->as.field_access.base = base;
    n->as.field_access.field = sdup(field);
    return n;
}

ASTNode* ast_new_call(ASTNode* callee, int line, int col) {
    ASTNode* n = ast_alloc(AST_CALL, line, col);
    n->as.call.callee = callee;
    ast_list_init(&n->as.call.args);
    return n;
}

// Fonctions d'aide (ajout)

void ast_block_add(ASTNode* block, ASTNode* stmt) {
    if (!block || block->kind != AST_BLOCK) return;
    ast_list_push(&block->as.block.stmts, stmt);
}

void ast_program_add_decl(ASTNode* program, ASTNode* decl) {
    if (!program || program->kind != AST_PROGRAM) return;
    ast_list_push(&program->as.program.decls, decl);
}

void ast_program_add_def(ASTNode* program, ASTNode* def) {
    if (!program || program->kind != AST_PROGRAM) return;
    ast_list_push(&program->as.program.defs, def);
}

// Libération mémoire

static void ast_free_list(ASTList* list) {
    for (int i = 0; i < list->count; i++) {
        ast_free(list->items[i]);
    }
    ast_list_free_shallow(list);
}

void ast_free(ASTNode* node) {
    if (!node) return;

    switch (node->kind) {
        case AST_PROGRAM:
            free(node->as.program.name);
            ast_free_list(&node->as.program.decls);
            ast_free_list(&node->as.program.defs);
            ast_free(node->as.program.main_block);
            break;

        case AST_DECL_VAR:
            free(node->as.decl_var.name);
            ast_free(node->as.decl_var.type);
            break;

        case AST_DECL_CONST:
            free(node->as.decl_const.name);
            ast_free(node->as.decl_const.type);
            ast_free(node->as.decl_const.value);
            break;

        case AST_DECL_ARRAY:
            free(node->as.decl_array.name);
            ast_free(node->as.decl_array.elem_type);
            ast_free_list(&node->as.decl_array.dims);
            break;

        case AST_TYPE_NAMED:
            free(node->as.type_named.name);
            break;

        case AST_TYPE_ARRAY:
           ast_free(node->as.type_array.elem_type);
           ast_free_list(&node->as.type_array.dims);
           break;


        case AST_DEF_STRUCT:
            free(node->as.def_struct.name);
            ast_free_list(&node->as.def_struct.fields);
            break;

        case AST_FIELD:
            free(node->as.field.name);
            ast_free(node->as.field.type);
            break;

        case AST_DEF_FUNC:
            free(node->as.def_func.name);
            ast_free_list(&node->as.def_func.params);
            ast_free(node->as.def_func.return_type);
            ast_free(node->as.def_func.body);
            break;

        case AST_DEF_PROC:
            free(node->as.def_proc.name);
            ast_free_list(&node->as.def_proc.params);
            ast_free(node->as.def_proc.body);
            break;

        case AST_PARAM:
            free(node->as.param.name);
            ast_free(node->as.param.type);
            break;

        case AST_BLOCK:
            ast_free_list(&node->as.block.stmts);
            break;

        case AST_ASSIGN:
            ast_free(node->as.assign.target);
            ast_free(node->as.assign.value);
            break;

        case AST_IF:
            ast_free(node->as.if_stmt.cond);
            ast_free(node->as.if_stmt.then_block);
            ast_free_list(&node->as.if_stmt.elif_conds);
            ast_free_list(&node->as.if_stmt.elif_blocks);
            ast_free(node->as.if_stmt.else_block);
            break;

        case AST_WHILE:
            ast_free(node->as.while_stmt.cond);
            ast_free(node->as.while_stmt.body);
            break;

        case AST_FOR:
            free(node->as.for_stmt.var);
            ast_free(node->as.for_stmt.start);
            ast_free(node->as.for_stmt.end);
            ast_free(node->as.for_stmt.step);
            ast_free(node->as.for_stmt.body);
            break;

        case AST_REPEAT:
            ast_free(node->as.repeat_stmt.body);
            ast_free(node->as.repeat_stmt.until_cond);
            break;

        case AST_WRITE:
            ast_free_list(&node->as.write_stmt.args);
            break;

        case AST_READ:
            ast_free_list(&node->as.read_stmt.targets);
            break;

        case AST_RETURN:
            ast_free(node->as.ret_stmt.value);
            break;

        case AST_CALL_STMT:
            ast_free(node->as.call_stmt.call);
            break;

        case AST_SWITCH:
            ast_free(node->as.switch_stmt.expr);
            ast_free_list(&node->as.switch_stmt.cases);
            ast_free(node->as.switch_stmt.default_block);
            break;

        case AST_CASE:
            ast_free_list(&node->as.case_stmt.values);
            ast_free(node->as.case_stmt.body);
            break;

        case AST_BINARY:
            ast_free(node->as.binary.lhs);
            ast_free(node->as.binary.rhs);
            break;

        case AST_UNARY:
            ast_free(node->as.unary.expr);
            break;

        case AST_LITERAL_REAL:
            free(node->as.lit_real.text);
            break;

        case AST_LITERAL_STRING:
            free(node->as.lit_string.text);
            break;

        case AST_IDENT:
            free(node->as.ident.name);
            break;

        case AST_INDEX:
            ast_free(node->as.index.base);
            ast_free(node->as.index.index);
            break;

        case AST_FIELD_ACCESS:
            ast_free(node->as.field_access.base);
            free(node->as.field_access.field);
            break;

        case AST_CALL:
            ast_free(node->as.call.callee);
            ast_free_list(&node->as.call.args);
            break;

        default:
            break;
    }

    free(node);
}

// Affichage (simple)

static void indent(int n) { for (int i = 0; i < n; i++) putchar(' '); }

static const char* prim_to_str(PrimitiveType p) {
    switch (p) {
        case TYPE_ENTIER: return "entier";
        case TYPE_REEL: return "reel";
        case TYPE_CARACTERE: return "caractere";
        case TYPE_CHAINE: return "chaine";
        case TYPE_BOOLEEN: return "booleen";
        default: return "?";
    }
}

static void ast_print_rec(ASTNode* n, int ind) {
    if (!n) { indent(ind); printf("(null)\n"); return; }

    indent(ind);
    printf("[%d:%d] ", n->line, n->col);

    switch (n->kind) {
        case AST_PROGRAM:
            printf("PROGRAM %s\n", n->as.program.name ? n->as.program.name : "(noname)");
            indent(ind+2); printf("DECLS(%d)\n", n->as.program.decls.count);
            for (int i=0;i<n->as.program.decls.count;i++) ast_print_rec(n->as.program.decls.items[i], ind+4);
            indent(ind+2); printf("DEFS(%d)\n", n->as.program.defs.count);
            for (int i=0;i<n->as.program.defs.count;i++) ast_print_rec(n->as.program.defs.items[i], ind+4);
            indent(ind+2); printf("MAIN\n");
            ast_print_rec(n->as.program.main_block, ind+4);
            break;

        case AST_DECL_VAR:
            printf("DECL_VAR %s\n", n->as.decl_var.name);
            ast_print_rec(n->as.decl_var.type, ind+2);
            break;

        case AST_DECL_CONST:
            printf("DECL_CONST %s\n", n->as.decl_const.name);
            ast_print_rec(n->as.decl_const.type, ind+2);
            ast_print_rec(n->as.decl_const.value, ind+2);
            break;

        case AST_DECL_ARRAY:
            printf("DECL_ARRAY %s\n", n->as.decl_array.name);
            ast_print_rec(n->as.decl_array.elem_type, ind+2);
            indent(ind+2); printf("DIMS(%d)\n", n->as.decl_array.dims.count);
            for (int i=0;i<n->as.decl_array.dims.count;i++) ast_print_rec(n->as.decl_array.dims.items[i], ind+4);
            break;

        case AST_TYPE_PRIMITIVE:
            printf("TYPE %s\n", prim_to_str(n->as.type_prim.prim));
            break;

        case AST_TYPE_NAMED:
            printf("TYPE %s\n", n->as.type_named.name);
            break;

        case AST_DEF_STRUCT:
            printf("STRUCT %s\n", n->as.def_struct.name);
            for (int i=0;i<n->as.def_struct.fields.count;i++) ast_print_rec(n->as.def_struct.fields.items[i], ind+2);
            break;

        case AST_FIELD:
            printf("FIELD %s\n", n->as.field.name);
            ast_print_rec(n->as.field.type, ind+2);
            break;

        case AST_DEF_FUNC:
            printf("FUNC %s\n", n->as.def_func.name);
            indent(ind+2); printf("PARAMS(%d)\n", n->as.def_func.params.count);
            for (int i=0;i<n->as.def_func.params.count;i++) ast_print_rec(n->as.def_func.params.items[i], ind+4);
            indent(ind+2); printf("RET\n");
            ast_print_rec(n->as.def_func.return_type, ind+4);
            indent(ind+2); printf("BODY\n");
            ast_print_rec(n->as.def_func.body, ind+4);
            break;

        case AST_DEF_PROC:
            printf("PROC %s\n", n->as.def_proc.name);
            indent(ind+2); printf("PARAMS(%d)\n", n->as.def_proc.params.count);
            for (int i=0;i<n->as.def_proc.params.count;i++) ast_print_rec(n->as.def_proc.params.items[i], ind+4);
            indent(ind+2); printf("BODY\n");
            ast_print_rec(n->as.def_proc.body, ind+4);
            break;

        case AST_PARAM:
            printf("PARAM %s\n", n->as.param.name);
            ast_print_rec(n->as.param.type, ind+2);
            break;

        case AST_BLOCK:
            printf("BLOCK(%d)\n", n->as.block.stmts.count);
            for (int i=0;i<n->as.block.stmts.count;i++) ast_print_rec(n->as.block.stmts.items[i], ind+2);
            break;

        case AST_ASSIGN:
            printf("ASSIGN\n");
            ast_print_rec(n->as.assign.target, ind+2);
            ast_print_rec(n->as.assign.value, ind+2);
            break;

        case AST_IF:
            printf("IF\n");
            ast_print_rec(n->as.if_stmt.cond, ind+2);
            ast_print_rec(n->as.if_stmt.then_block, ind+2);
            for (int i=0;i<n->as.if_stmt.elif_conds.count;i++) {
                indent(ind); printf("ELIF\n");
                ast_print_rec(n->as.if_stmt.elif_conds.items[i], ind+2);
                ast_print_rec(n->as.if_stmt.elif_blocks.items[i], ind+2);
            }
            if (n->as.if_stmt.else_block) {
                indent(ind); printf("ELSE\n");
                ast_print_rec(n->as.if_stmt.else_block, ind+2);
            }
            break;
        case AST_CALL_STMT:
            printf("CALL_STMT\n");
            ast_print_rec(n->as.call_stmt.call, ind+2);
            break;

        case AST_WHILE:
            printf("WHILE\n");
            ast_print_rec(n->as.while_stmt.cond, ind+2);
            ast_print_rec(n->as.while_stmt.body, ind+2);
            break;

        case AST_FOR:
            printf("FOR %s\n", n->as.for_stmt.var);
            ast_print_rec(n->as.for_stmt.start, ind+2);
            ast_print_rec(n->as.for_stmt.end, ind+2);
            if (n->as.for_stmt.step) ast_print_rec(n->as.for_stmt.step, ind+2);
            ast_print_rec(n->as.for_stmt.body, ind+2);
            break;

        case AST_WRITE:
            printf("ECRIRE\n");
            for (int i=0;i<n->as.write_stmt.args.count;i++) ast_print_rec(n->as.write_stmt.args.items[i], ind+2);
            break;

        case AST_READ:
            printf("LIRE\n");
            for (int i=0;i<n->as.read_stmt.targets.count;i++) ast_print_rec(n->as.read_stmt.targets.items[i], ind+2);
            break;

        case AST_RETURN:
            printf("RETURN\n");
            ast_print_rec(n->as.ret_stmt.value, ind+2);
            break;

        case AST_SWITCH:
            printf("SELON\n");
            ast_print_rec(n->as.switch_stmt.expr, ind+2);
            for (int i=0;i<n->as.switch_stmt.cases.count;i++) ast_print_rec(n->as.switch_stmt.cases.items[i], ind+2);
            if (n->as.switch_stmt.default_block) {
                indent(ind+2); printf("DEFAUT\n");
                ast_print_rec(n->as.switch_stmt.default_block, ind+4);
            }
            break;

        case AST_CASE:
            printf("CAS values(%d)\n", n->as.case_stmt.values.count);
            for (int i=0;i<n->as.case_stmt.values.count;i++) ast_print_rec(n->as.case_stmt.values.items[i], ind+2);
            ast_print_rec(n->as.case_stmt.body, ind+2);
            break;

        case AST_BINARY:
            printf("BINOP %s\n", token_to_string(n->as.binary.op));
            ast_print_rec(n->as.binary.lhs, ind+2);
            ast_print_rec(n->as.binary.rhs, ind+2);
            break;

        case AST_UNARY:
            printf("UNARY %s\n", token_to_string(n->as.unary.op));
            ast_print_rec(n->as.unary.expr, ind+2);
            break;

        case AST_LITERAL_INT:
            printf("INT %lld\n", n->as.lit_int.value);
            break;

        case AST_LITERAL_REAL:
            printf("REAL %s\n", n->as.lit_real.text);
            break;

        case AST_LITERAL_STRING:
            printf("STRING \"%s\"\n", n->as.lit_string.text);
            break;

        case AST_LITERAL_BOOL:
            printf("BOOL %s\n", n->as.lit_bool.value ? "vrai" : "faux");
            break;

        case AST_IDENT:
            printf("ID %s\n", n->as.ident.name);
            break;

        case AST_INDEX:
            printf("INDEX\n");
            ast_print_rec(n->as.index.base, ind+2);
            ast_print_rec(n->as.index.index, ind+2);
            break;

        case AST_FIELD_ACCESS:
            printf("FIELD .%s\n", n->as.field_access.field);
            ast_print_rec(n->as.field_access.base, ind+2);
            break;

        case AST_CALL:
            printf("CALL\n");
            ast_print_rec(n->as.call.callee, ind+2);
            for (int i=0;i<n->as.call.args.count;i++) ast_print_rec(n->as.call.args.items[i], ind+2);
            break;

        case AST_BREAK:
            printf("SORTIR\n");
            break;

        case AST_QUIT_FOR:
            printf("QUITTER_POUR\n");
            break;

        default:
            printf("NODE kind=%d\n", (int)n->kind);
            break;
    }
}

void ast_print(ASTNode* node) {
    ast_print_rec(node, 0);
}
