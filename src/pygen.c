#include "pygen.h"
#include "token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

/* String builder */

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Str;

static void str_init(Str* s) { s->data = NULL; s->len = 0; s->cap = 0; }

static void str_grow(Str* s, size_t add) {
    if (s->len + add + 1 > s->cap) {
        size_t nc = (s->cap == 0) ? 1024 : s->cap * 2;
        while (nc < s->len + add + 1) nc *= 2;
        s->data = (char*)realloc(s->data, nc);
        s->cap = nc;
    }
}

static void str_append(Str* s, const char* t) {
    if (!t) return;
    size_t n = strlen(t);
    str_grow(s, n);
    memcpy(s->data + s->len, t, n);
    s->len += n;
    s->data[s->len] = '\0';
}

static void str_printf(Str* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[8192];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str_append(s, buf);
}

static void str_free(Str* s) {
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

static char* dupstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Types + symboles (pour READ + init) */

typedef enum { PT_UNKNOWN, PT_INT, PT_FLOAT, PT_BOOL, PT_CHAR, PT_STRING, PT_STRUCT, PT_ARRAY } PTypeKind;

typedef struct PType {
    PTypeKind kind;
    char* struct_name;
    struct PType* elem;
    int dims;
} PType;

static PType* pt_new(PTypeKind k) {
    PType* t = (PType*)calloc(1, sizeof(PType));
    if (t) t->kind = k;
    return t;
}

static PType* pt_clone(const PType* src) {
    if (!src) return pt_new(PT_UNKNOWN);
    PType* t = pt_new(src->kind);
    if (src->struct_name) t->struct_name = dupstr(src->struct_name);
    if (src->elem) t->elem = pt_clone(src->elem);
    t->dims = src->dims;
    return t;
}

static void pt_free(PType* t) {
    if (!t) return;
    free(t->struct_name);
    pt_free(t->elem);
    free(t);
}

typedef struct { char* name; PType* type; } Sym;
typedef struct { Sym* items; int count; int cap; } SymTab;

static void symtab_add(SymTab* st, const char* name, PType* type) {
    if (!st || !name || !type) return;
    if (st->count >= st->cap) {
        st->cap = (st->cap == 0) ? 16 : st->cap * 2;
        st->items = (Sym*)realloc(st->items, (size_t)st->cap * sizeof(Sym));
    }
    st->items[st->count].name = dupstr(name);
    st->items[st->count].type = pt_clone(type);
    st->count++;
}

static PType* symtab_lookup(SymTab* st, const char* name) {
    if (!st || !name) return NULL;
    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->items[i].name, name) == 0) return st->items[i].type;
    }
    return NULL;
}

static void symtab_free(SymTab* st) {
    if (!st) return;
    for (int i = 0; i < st->count; i++) {
        free(st->items[i].name);
        pt_free(st->items[i].type);
    }
    free(st->items);
    st->items = NULL;
    st->count = st->cap = 0;
}

/* Générateur Python */

typedef struct {
    Str out;
    int indent;

    /* structs: nom + fields */
    struct { char* name; SymTab fields; }* structs;
    int struct_count;

    /* funcs: nom + ret */
    struct { char* name; PType* ret; }* funcs;
    int func_count;

    /* scopes vars */
    SymTab* scopes;
    int scope_count;

    int tmp_id; /* noms temporaires uniques */

} PG;

static void emit_indent(PG* pg) { for (int i = 0; i < pg->indent; i++) str_append(&pg->out, "    "); }
static void emit_ln(PG* pg, const char* s) { emit_indent(pg); str_append(&pg->out, s); str_append(&pg->out, "\n"); }

static void push_scope(PG* pg) {
    pg->scopes = (SymTab*)realloc(pg->scopes, (size_t)(pg->scope_count + 1) * sizeof(SymTab));
    memset(&pg->scopes[pg->scope_count], 0, sizeof(SymTab));
    pg->scope_count++;
}

static void pop_scope(PG* pg) {
    if (pg->scope_count <= 0) return;
    pg->scope_count--;
    symtab_free(&pg->scopes[pg->scope_count]);
}

static void tmp_name(PG* pg, const char* prefix, char* buf, size_t n) {
    snprintf(buf, n, "%s%d", prefix, pg->tmp_id++);
}

static PType* lookup_var(PG* pg, const char* name) {
    for (int i = pg->scope_count - 1; i >= 0; i--) {
        PType* t = symtab_lookup(&pg->scopes[i], name);
        if (t) return pt_clone(t);
    }
    return NULL;
}

static PType* lookup_func_ret(PG* pg, const char* name) {
    for (int i = 0; i < pg->func_count; i++) {
        if (strcmp(pg->funcs[i].name, name) == 0) return pt_clone(pg->funcs[i].ret);
    }
    return NULL;
}

static PType* lookup_struct_field(PG* pg, const char* struct_name, const char* field) {
    for (int i = 0; i < pg->struct_count; i++) {
        if (strcmp(pg->structs[i].name, struct_name) == 0) {
            PType* t = symtab_lookup(&pg->structs[i].fields, field);
            return t ? pt_clone(t) : NULL;
        }
    }
    return NULL;
}

/* AST -> PType */

static PType* ast_to_ptype(ASTNode* t) {
    if (!t) return pt_new(PT_UNKNOWN);

    if (t->kind == AST_TYPE_PRIMITIVE) {
        switch (t->as.type_prim.prim) {
            case TYPE_ENTIER: return pt_new(PT_INT);
            case TYPE_REEL: return pt_new(PT_FLOAT);
            case TYPE_BOOLEEN: return pt_new(PT_BOOL);
            case TYPE_CARACTERE: return pt_new(PT_CHAR);
            case TYPE_CHAINE: return pt_new(PT_STRING);
            default: return pt_new(PT_UNKNOWN);
        }
    }

    if (t->kind == AST_TYPE_NAMED) {
        PType* p = pt_new(PT_STRUCT);
        p->struct_name = dupstr(t->as.type_named.name);
        return p;
    }

    if (t->kind == AST_TYPE_ARRAY) {
        PType* p = pt_new(PT_ARRAY);
        p->elem = ast_to_ptype(t->as.type_array.elem_type);
        p->dims = t->as.type_array.dims.count;
        return p;
    }

    return pt_new(PT_UNKNOWN);
}

/* Inférence expr (pour READ) */

static PType* infer_expr(PG* pg, ASTNode* e) {
    if (!e) return pt_new(PT_UNKNOWN);

    switch (e->kind) {
        case AST_LITERAL_INT: return pt_new(PT_INT);
        case AST_LITERAL_REAL: return pt_new(PT_FLOAT);
        case AST_LITERAL_BOOL: return pt_new(PT_BOOL);
        case AST_LITERAL_STRING: return pt_new(PT_STRING);

        case AST_IDENT: {
            PType* t = lookup_var(pg, e->as.ident.name);
            return t ? t : pt_new(PT_UNKNOWN);
        }

        case AST_UNARY:
            if (e->as.unary.op == TOK_NON) return pt_new(PT_BOOL);
            return infer_expr(pg, e->as.unary.expr);

        case AST_BINARY: {
            TokenType op = e->as.binary.op;
            if ((op >= TOK_INFERIEUR && op <= TOK_DIFFERENT) || op == TOK_ET || op == TOK_OU) return pt_new(PT_BOOL);
            PType* l = infer_expr(pg, e->as.binary.lhs);
            PType* r = infer_expr(pg, e->as.binary.rhs);
            bool want_float = (l->kind == PT_FLOAT || r->kind == PT_FLOAT || op == TOK_DIVISE);
            pt_free(l); pt_free(r);
            return want_float ? pt_new(PT_FLOAT) : pt_new(PT_INT);
        }

        case AST_CALL:
            if (e->as.call.callee && e->as.call.callee->kind == AST_IDENT) {
                PType* t = lookup_func_ret(pg, e->as.call.callee->as.ident.name);
                return t ? t : pt_new(PT_UNKNOWN);
            }
            return pt_new(PT_UNKNOWN);

        case AST_FIELD_ACCESS: {
            PType* b = infer_expr(pg, e->as.field_access.base);
            if (b && b->kind == PT_STRUCT && b->struct_name) {
                PType* f = lookup_struct_field(pg, b->struct_name, e->as.field_access.field);
                pt_free(b);
                return f ? f : pt_new(PT_UNKNOWN);
            }
            pt_free(b);
            return pt_new(PT_UNKNOWN);
        }

        case AST_INDEX: {
            PType* b = infer_expr(pg, e->as.index.base);
            if (b && b->kind == PT_ARRAY && b->elem) {
                if (b->dims > 1) {
                    PType* n = pt_clone(b);
                    n->dims--;
                    pt_free(b);
                    return n;
                } else {
                    PType* el = pt_clone(b->elem);
                    pt_free(b);
                    return el;
                }
            }
            pt_free(b);
            return pt_new(PT_UNKNOWN);
        }

        default:
            return pt_new(PT_UNKNOWN);
    }
}

/* Expressions Python */

static void emit_expr(PG* pg, ASTNode* e);

static void emit_binop(PG* pg, TokenType op) {
    switch (op) {
        case TOK_PLUS: str_append(&pg->out, " + "); break;
        case TOK_MOINS: str_append(&pg->out, " - "); break;
        case TOK_FOIS: str_append(&pg->out, " * "); break;
        case TOK_DIVISE: str_append(&pg->out, " / "); break;
        case TOK_DIV_ENTIER: str_append(&pg->out, " // "); break;
        case TOK_MODULO: str_append(&pg->out, " % "); break;
        case TOK_PUISSANCE: str_append(&pg->out, " ** "); break;

        case TOK_EGAL: str_append(&pg->out, " == "); break;
        case TOK_DIFFERENT: str_append(&pg->out, " != "); break;
        case TOK_INFERIEUR: str_append(&pg->out, " < "); break;
        case TOK_INFERIEUR_EGAL: str_append(&pg->out, " <= "); break;
        case TOK_SUPERIEUR: str_append(&pg->out, " > "); break;
        case TOK_SUPERIEUR_EGAL: str_append(&pg->out, " >= "); break;

        case TOK_ET: str_append(&pg->out, " and "); break;
        case TOK_OU: str_append(&pg->out, " or "); break;
        default: break;
    }
}

static void emit_string_literal(PG* pg, const char* s) {
    str_append(&pg->out, "\"");
    if (!s) { str_append(&pg->out, "\""); return; }
    for (const char* p = s; *p; p++) {
        if (*p == '\\') str_append(&pg->out, "\\\\");
        else if (*p == '"') str_append(&pg->out, "\\\"");
        else if (*p == '\n') str_append(&pg->out, "\\n");
        else if (*p == '\t') str_append(&pg->out, "\\t");
        else { char tmp[2] = {*p, 0}; str_append(&pg->out, tmp); }
    }
    str_append(&pg->out, "\"");
}

static void emit_expr(PG* pg, ASTNode* e) {
    if (!e) { str_append(&pg->out, "None"); return; }

    switch (e->kind) {
        case AST_LITERAL_INT: str_printf(&pg->out, "%lld", e->as.lit_int.value); break;
        case AST_LITERAL_REAL: str_append(&pg->out, e->as.lit_real.text ? e->as.lit_real.text : "0.0"); break;
        case AST_LITERAL_BOOL: str_append(&pg->out, e->as.lit_bool.value ? "True" : "False"); break;
        case AST_LITERAL_STRING: emit_string_literal(pg, e->as.lit_string.text); break;
        case AST_IDENT: str_append(&pg->out, e->as.ident.name); break;

        case AST_UNARY:
            if (e->as.unary.op == TOK_NON) str_append(&pg->out, "not ");
            else if (e->as.unary.op == TOK_MOINS) str_append(&pg->out, "-");
            str_append(&pg->out, "(");
            emit_expr(pg, e->as.unary.expr);
            str_append(&pg->out, ")");
            break;

        case AST_BINARY:
            str_append(&pg->out, "(");
            emit_expr(pg, e->as.binary.lhs);
            emit_binop(pg, e->as.binary.op);
            emit_expr(pg, e->as.binary.rhs);
            str_append(&pg->out, ")");
            break;

        case AST_CALL:
            emit_expr(pg, e->as.call.callee);
            str_append(&pg->out, "(");
            for (int i = 0; i < e->as.call.args.count; i++) {
                if (i > 0) str_append(&pg->out, ", ");
                emit_expr(pg, e->as.call.args.items[i]);
            }
            str_append(&pg->out, ")");
            break;

        case AST_INDEX:
            emit_expr(pg, e->as.index.base);
            str_append(&pg->out, "[");
            emit_expr(pg, e->as.index.index);
            str_append(&pg->out, "]");
            break;

        case AST_FIELD_ACCESS:
            emit_expr(pg, e->as.field_access.base);
            str_append(&pg->out, ".");
            str_append(&pg->out, e->as.field_access.field);
            break;

        default:
            str_append(&pg->out, "None");
            break;
    }
}

/* Valeur par défaut Python */

static void emit_default_value(PG* pg, PType* t) {
    if (!t) { str_append(&pg->out, "None"); return; }
    switch (t->kind) {
        case PT_INT: str_append(&pg->out, "0"); break;
        case PT_FLOAT: str_append(&pg->out, "0.0"); break;
        case PT_BOOL: str_append(&pg->out, "False"); break;
        case PT_CHAR: str_append(&pg->out, "'\\0'"); break;
        case PT_STRING: str_append(&pg->out, "\"\""); break;
        case PT_STRUCT:
            if (t->struct_name) { str_append(&pg->out, t->struct_name); str_append(&pg->out, "()"); }
            else str_append(&pg->out, "None");
            break;
        default:
            str_append(&pg->out, "None");
            break;
    }
}

/* Init tableaux (list comprehensions), sans helper */

static void emit_array_init_expr(PG* pg, PType* elem_type, ASTNode** dims, int dim_count) {
    /* génère une expression du type:
       [ <rec(dim2..)> for _i0 in range(dim0) ]
       [[ <rec(dim3..)> for _i1 in range(dim1) ] for _i0 in range(dim0)]
    */
    if (dim_count <= 0) {
        emit_default_value(pg, elem_type);
        return;
    }

    char idx[32];
    tmp_name(pg, "_i", idx, sizeof(idx));

    str_append(&pg->out, "[");
    if (dim_count == 1) {
        emit_default_value(pg, elem_type);
        str_append(&pg->out, " for ");
        str_append(&pg->out, idx);
        str_append(&pg->out, " in range(int(");
        emit_expr(pg, dims[0]);
        str_append(&pg->out, "))]");
        return;
    }

    emit_array_init_expr(pg, elem_type, dims + 1, dim_count - 1);
    str_append(&pg->out, " for ");
    str_append(&pg->out, idx);
    str_append(&pg->out, " in range(int(");
    emit_expr(pg, dims[0]);
    str_append(&pg->out, "))]");
}

/* Déclarations */

static void emit_decl(PG* pg, ASTNode* d, bool is_global) {
    (void)is_global;

    if (!d) return;
    const char* name = NULL;
    ASTNode* typeNode = NULL;

    if (d->kind == AST_DECL_VAR) { name = d->as.decl_var.name; typeNode = d->as.decl_var.type; }
    else if (d->kind == AST_DECL_CONST) { name = d->as.decl_const.name; typeNode = d->as.decl_const.type; }
    else if (d->kind == AST_DECL_ARRAY) { name = d->as.decl_array.name; typeNode = d->as.decl_array.elem_type; }
    else return;

    PType* t = ast_to_ptype(typeNode);

    if (d->kind == AST_DECL_ARRAY) {
        PType* arr = pt_new(PT_ARRAY);
        arr->elem = t;
        arr->dims = d->as.decl_array.dims.count;
        t = arr;
    }

    symtab_add(&pg->scopes[pg->scope_count - 1], name, t);

    emit_indent(pg);
    str_append(&pg->out, name);
    str_append(&pg->out, " = ");

    if (d->kind == AST_DECL_CONST) {
        emit_expr(pg, d->as.decl_const.value);
        str_append(&pg->out, "\n");
        pt_free(t);
        return;
    }

    if (d->kind == AST_DECL_ARRAY) {
        emit_array_init_expr(pg, t->elem, d->as.decl_array.dims.items, d->as.decl_array.dims.count);
        str_append(&pg->out, "\n");
        pt_free(t);
        return;
    }

    emit_default_value(pg, t);
    str_append(&pg->out, "\n");
    pt_free(t);
}

/* Statements / Blocks */

static void emit_block(PG* pg, ASTNode* b);
static void emit_stmt(PG* pg, ASTNode* s);

static void emit_write(PG* pg, ASTNode* s) {
    emit_indent(pg);
    if (s->as.write_stmt.args.count <= 1) {
        str_append(&pg->out, "print(");
        if (s->as.write_stmt.args.count == 1) emit_expr(pg, s->as.write_stmt.args.items[0]);
        str_append(&pg->out, ")\n");
        return;
    }

    str_append(&pg->out, "print(");
    for (int i = 0; i < s->as.write_stmt.args.count; i++) {
        if (i > 0) str_append(&pg->out, ", ");
        emit_expr(pg, s->as.write_stmt.args.items[i]);
    }
    str_append(&pg->out, ", sep=\"\")\n");
}

static void emit_read_one(PG* pg, ASTNode* target) {
    PType* t = infer_expr(pg, target);

    emit_indent(pg);
    emit_expr(pg, target);
    str_append(&pg->out, " = ");

    if (t->kind == PT_INT) {
        str_append(&pg->out, "int(input())\n");
    } else if (t->kind == PT_FLOAT) {
        str_append(&pg->out, "float(input())\n");
    } else if (t->kind == PT_BOOL) {
        char tmp[32]; tmp_name(pg, "_s", tmp, sizeof(tmp));
        str_printf(&pg->out,
            "(lambda %s: (%s == \"true\" or %s == \"1\"))((input().strip().lower()))\n",
            tmp, tmp, tmp);
    } else if (t->kind == PT_CHAR) {
        str_append(&pg->out, "(input()[:1] or \"\\0\")\n");
    } else {
        str_append(&pg->out, "input()\n");
    }

    pt_free(t);
}

static void emit_switch(PG* pg, ASTNode* s) {
    char tmpv[32];
    tmp_name(pg, "s", tmpv, sizeof(tmpv));

    emit_indent(pg);
    str_printf(&pg->out, "%s = ", tmpv);
    emit_expr(pg, s->as.switch_stmt.expr);
    str_append(&pg->out, "\n");

    bool first = true;
    for (int i = 0; i < s->as.switch_stmt.cases.count; i++) {
        ASTNode* c = s->as.switch_stmt.cases.items[i];
        if (!c || c->kind != AST_CASE) continue;

        emit_indent(pg);
        str_append(&pg->out, first ? "if " : "elif ");
        first = false;

        for (int j = 0; j < c->as.case_stmt.values.count; j++) {
            if (j > 0) str_append(&pg->out, " or ");
            str_append(&pg->out, tmpv);
            str_append(&pg->out, " == ");
            emit_expr(pg, c->as.case_stmt.values.items[j]);
        }

        str_append(&pg->out, ":\n");
        pg->indent++;
        emit_block(pg, c->as.case_stmt.body);
        pg->indent--;
    }

    if (s->as.switch_stmt.default_block) {
        emit_indent(pg);
        str_append(&pg->out, "else:\n");
        pg->indent++;
        emit_block(pg, s->as.switch_stmt.default_block);
        pg->indent--;
    }
}

static void emit_stmt(PG* pg, ASTNode* s) {
    if (!s) return;

    switch (s->kind) {
        case AST_DECL_VAR:
        case AST_DECL_CONST:
        case AST_DECL_ARRAY:
            emit_decl(pg, s, false);
            break;

        case AST_ASSIGN:
            emit_indent(pg);
            emit_expr(pg, s->as.assign.target);
            str_append(&pg->out, " = ");
            emit_expr(pg, s->as.assign.value);
            str_append(&pg->out, "\n");
            break;

        case AST_CALL_STMT:
            emit_indent(pg);
            emit_expr(pg, s->as.call_stmt.call);
            str_append(&pg->out, "\n");
            break;

        case AST_RETURN:
            emit_indent(pg);
            str_append(&pg->out, "return");
            if (s->as.ret_stmt.value) {
                str_append(&pg->out, " ");
                emit_expr(pg, s->as.ret_stmt.value);
            }
            str_append(&pg->out, "\n");
            break;

        case AST_WRITE:
            emit_write(pg, s);
            break;

        case AST_READ:
            for (int i = 0; i < s->as.read_stmt.targets.count; i++)
                emit_read_one(pg, s->as.read_stmt.targets.items[i]);
            break;

        case AST_IF:
            emit_indent(pg);
            str_append(&pg->out, "if ");
            emit_expr(pg, s->as.if_stmt.cond);
            str_append(&pg->out, ":\n");
            pg->indent++;
            emit_block(pg, s->as.if_stmt.then_block);
            pg->indent--;

            for (int i = 0; i < s->as.if_stmt.elif_conds.count; i++) {
                emit_indent(pg);
                str_append(&pg->out, "elif ");
                emit_expr(pg, s->as.if_stmt.elif_conds.items[i]);
                str_append(&pg->out, ":\n");
                pg->indent++;
                emit_block(pg, s->as.if_stmt.elif_blocks.items[i]);
                pg->indent--;
            }

            if (s->as.if_stmt.else_block) {
                emit_indent(pg);
                str_append(&pg->out, "else:\n");
                pg->indent++;
                emit_block(pg, s->as.if_stmt.else_block);
                pg->indent--;
            }
            break;

        case AST_WHILE:
            emit_indent(pg);
            str_append(&pg->out, "while ");
            emit_expr(pg, s->as.while_stmt.cond);
            str_append(&pg->out, ":\n");
            pg->indent++;
            emit_block(pg, s->as.while_stmt.body);
            pg->indent--;
            break;

        case AST_FOR: {
            emit_indent(pg);
            str_append(&pg->out, "for ");
            str_append(&pg->out, s->as.for_stmt.var);
            str_append(&pg->out, " in range(");

            emit_expr(pg, s->as.for_stmt.start);

            str_append(&pg->out, ", ");
            emit_expr(pg, s->as.for_stmt.end);
            str_append(&pg->out, " + 1");

            if (s->as.for_stmt.step) {
                str_append(&pg->out, ", ");
                emit_expr(pg, s->as.for_stmt.step);
            }

            str_append(&pg->out, "):\n");

            pg->indent++;
            emit_block(pg, s->as.for_stmt.body);
            pg->indent--;
            break;
        }

        case AST_REPEAT:
            emit_indent(pg);
            str_append(&pg->out, "while True:\n");
            pg->indent++;
            emit_block(pg, s->as.repeat_stmt.body);

            emit_indent(pg);
            str_append(&pg->out, "if ");
            if (s->as.repeat_stmt.until_cond) emit_expr(pg, s->as.repeat_stmt.until_cond);
            else str_append(&pg->out, "True");
            str_append(&pg->out, ":\n");

            pg->indent++;
            emit_indent(pg);
            str_append(&pg->out, "break\n");
            pg->indent -= 2;
            break;

        case AST_SWITCH:
            emit_switch(pg, s);
            break;

        case AST_BREAK:
        case AST_QUIT_FOR:
            emit_indent(pg);
            str_append(&pg->out, "break\n");
            break;

        case AST_BLOCK:
            emit_block(pg, s);
            break;

        default:
            break;
    }
}

static void emit_block(PG* pg, ASTNode* b) {
    if (!b || b->kind != AST_BLOCK) {
        emit_indent(pg);
        str_append(&pg->out, "pass\n");
        return;
    }

    if (b->as.block.stmts.count == 0) {
        emit_indent(pg);
        str_append(&pg->out, "pass\n");
        return;
    }

    push_scope(pg);

    for (int i = 0; i < b->as.block.stmts.count; i++) {
        ASTNode* st = b->as.block.stmts.items[i];
        if (!st) continue;
        if (st->kind == AST_DECL_VAR || st->kind == AST_DECL_CONST || st->kind == AST_DECL_ARRAY)
            emit_stmt(pg, st);
    }

    for (int i = 0; i < b->as.block.stmts.count; i++) {
        ASTNode* st = b->as.block.stmts.items[i];
        if (!st) continue;
        if (st->kind == AST_DECL_VAR || st->kind == AST_DECL_CONST || st->kind == AST_DECL_ARRAY) continue;
        emit_stmt(pg, st);
    }

    pop_scope(pg);
}

/* Pré-déclaration fonctions + structs */

static void predeclare(PG* pg, ASTNode* program) {
    pg->funcs = (void*)calloc((size_t)program->as.program.defs.count, sizeof(*pg->funcs));
    pg->structs = (void*)calloc((size_t)program->as.program.defs.count, sizeof(*pg->structs));

    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;

        if (d->kind == AST_DEF_FUNC) {
            int k = pg->func_count++;
            pg->funcs[k].name = dupstr(d->as.def_func.name);
            pg->funcs[k].ret = ast_to_ptype(d->as.def_func.return_type);
        } else if (d->kind == AST_DEF_PROC) {
            int k = pg->func_count++;
            pg->funcs[k].name = dupstr(d->as.def_proc.name);
            pg->funcs[k].ret = pt_new(PT_UNKNOWN);
        } else if (d->kind == AST_DEF_STRUCT) {
            int sidx = pg->struct_count++;
            pg->structs[sidx].name = dupstr(d->as.def_struct.name);
        }
    }
}

static void emit_structs(PG* pg, ASTNode* program) {
    bool has_structs = false;
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (d && d->kind == AST_DEF_STRUCT) { has_structs = true; break; }
    }
    if (!has_structs) return;

    emit_ln(pg, "# Structures");
    emit_ln(pg, "");

    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d || d->kind != AST_DEF_STRUCT) continue;

        int idx = -1;
        for (int k = 0; k < pg->struct_count; k++) {
            if (strcmp(pg->structs[k].name, d->as.def_struct.name) == 0) { idx = k; break; }
        }

        str_printf(&pg->out, "class %s:\n", d->as.def_struct.name);
        pg->indent++;

        emit_ln(pg, "def __init__(self):");
        pg->indent++;

        if (d->as.def_struct.fields.count == 0) {
            emit_ln(pg, "pass");
        } else {
            for (int j = 0; j < d->as.def_struct.fields.count; j++) {
                ASTNode* f = d->as.def_struct.fields.items[j];
                if (!f || f->kind != AST_FIELD) continue;

                PType* ft = ast_to_ptype(f->as.field.type);
                if (idx >= 0) symtab_add(&pg->structs[idx].fields, f->as.field.name, ft);

                emit_indent(pg);
                str_printf(&pg->out, "self.%s = ", f->as.field.name);
                emit_default_value(pg, ft);
                str_append(&pg->out, "\n");

                pt_free(ft);
            }
        }

        pg->indent -= 2;
        emit_ln(pg, "");
    }
}

/* Fonctions/Procédures + Main */

static void emit_funcproc(PG* pg, ASTNode* def) {
    bool isFunc = (def->kind == AST_DEF_FUNC);
    const char* name = isFunc ? def->as.def_func.name : def->as.def_proc.name;
    ASTList* params = isFunc ? &def->as.def_func.params : &def->as.def_proc.params;
    ASTNode* body = isFunc ? def->as.def_func.body : def->as.def_proc.body;

    pg->tmp_id = 0;

    emit_indent(pg);
    str_append(&pg->out, "def ");
    str_append(&pg->out, name);
    str_append(&pg->out, "(");

    for (int i = 0; i < params->count; i++) {
        if (i > 0) str_append(&pg->out, ", ");
        str_append(&pg->out, params->items[i]->as.param.name);
    }
    str_append(&pg->out, "):\n");

    pg->indent++;
    push_scope(pg);

    for (int i = 0; i < params->count; i++) {
        ASTNode* p = params->items[i];
        PType* pt = ast_to_ptype(p->as.param.type);
        symtab_add(&pg->scopes[pg->scope_count - 1], p->as.param.name, pt);
        pt_free(pt);
    }

    emit_block(pg, body);

    pop_scope(pg);
    pg->indent--;

    emit_ln(pg, "");
}

/* Génération globale */

bool pygen_generate(ASTNode* program, const char* output_path) {
    if (!program || program->kind != AST_PROGRAM) return false;

    PG pg;
    memset(&pg, 0, sizeof(pg));
    str_init(&pg.out);
    pg.indent = 0;
    pg.tmp_id = 0;

    push_scope(&pg);
    predeclare(&pg, program);

    emit_ln(&pg, "# Generated Python code");
    emit_ln(&pg, "import math");
    emit_ln(&pg, "");

    emit_structs(&pg, program);

    emit_ln(&pg, "# Globales");
    for (int i = 0; i < program->as.program.decls.count; i++) {
        ASTNode* d = program->as.program.decls.items[i];
        if (!d) continue;
        emit_decl(&pg, d, true);
    }
    emit_ln(&pg, "");

    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;
        if (d->kind == AST_DEF_FUNC || d->kind == AST_DEF_PROC) emit_funcproc(&pg, d);
    }

    emit_ln(&pg, "def main():");
    pg.indent++;
    pg.tmp_id = 0;
    emit_block(&pg, program->as.program.main_block);
    pg.indent--;
    emit_ln(&pg, "");
    emit_ln(&pg, "if __name__ == \"__main__\":");
    pg.indent++;
    emit_ln(&pg, "main()");
    pg.indent--;

    FILE* f = fopen(output_path, "w");
    if (!f) {
        str_free(&pg.out);
        return false;
    }

    fputs(pg.out.data ? pg.out.data : "", f);
    fclose(f);

    str_free(&pg.out);

    for (int i = 0; i < pg.struct_count; i++) {
        free(pg.structs[i].name);
        symtab_free(&pg.structs[i].fields);
    }
    for (int i = 0; i < pg.func_count; i++) {
        free(pg.funcs[i].name);
        pt_free(pg.funcs[i].ret);
    }
    free(pg.structs);
    free(pg.funcs);

    while (pg.scope_count > 0) pop_scope(&pg);
    free(pg.scopes);

    return true;
}
