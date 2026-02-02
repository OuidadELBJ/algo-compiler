#include "jgen.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

/* Utilitaires chaînes */

static char* dupstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

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
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str_append(s, buf);
}

static void str_free(Str* s) { free(s->data); s->data = NULL; s->len = s->cap = 0; }

/* Types Java + symboles */

typedef enum { JT_UNKNOWN, JT_INT, JT_DOUBLE, JT_BOOL, JT_CHAR, JT_STRING, JT_STRUCT, JT_ARRAY } JTypeKind;

typedef struct JType {
    JTypeKind kind;
    char* struct_name;
    struct JType* elem;
    int dims;
} JType;

static JType* jt_new(JTypeKind k) {
    JType* t = (JType*)calloc(1, sizeof(JType));
    if (t) t->kind = k;
    return t;
}

static JType* jt_clone(const JType* src) {
    if (!src) return jt_new(JT_UNKNOWN);
    JType* t = jt_new(src->kind);
    if (src->struct_name) t->struct_name = dupstr(src->struct_name);
    if (src->elem) t->elem = jt_clone(src->elem);
    t->dims = src->dims;
    return t;
}

static void jt_free(JType* t) {
    if (!t) return;
    free(t->struct_name);
    jt_free(t->elem);
    free(t);
}

typedef struct { char* name; JType* type; } Sym;
typedef struct { Sym* items; int count; int cap; } SymTab;

static void symtab_add(SymTab* st, const char* name, JType* type) {
    if (!st || !name || !type) return;
    if (st->count >= st->cap) {
        st->cap = (st->cap == 0) ? 16 : st->cap * 2;
        st->items = (Sym*)realloc(st->items, (size_t)st->cap * sizeof(Sym));
    }
    st->items[st->count].name = dupstr(name);
    st->items[st->count].type = jt_clone(type);
    st->count++;
}

static JType* symtab_lookup(SymTab* st, const char* name) {
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
        jt_free(st->items[i].type);
    }
    free(st->items);
    st->items = NULL;
    st->count = st->cap = 0;
}

/* Générateur Java */

typedef struct {
    Str out;
    int indent;

    struct { char* name; SymTab fields; }* structs;
    int struct_count;

    struct { char* name; JType* ret; }* funcs;
    int func_count;

    SymTab* scopes;
    int scope_count;

    const char* class_name;

    /* Compteur pour noms temporaires uniques (Java interdit le shadowing) */
    int tmp_id;

    /* Liste des tableaux globaux de structs à initialiser dans static { } */
    struct {
        char* name;
        char* struct_name;
        ASTNode** dims;
        int dim_count;
    } *g_arr_inits;
    int g_arr_init_count;
    int g_arr_init_cap;

} JG;

/* Helpers indentation / scopes */

static void emit_indent(JG* jg) { for (int i = 0; i < jg->indent; i++) str_append(&jg->out, "    "); }
static void emit_ln(JG* jg, const char* s) { emit_indent(jg); str_append(&jg->out, s); str_append(&jg->out, "\n"); }

static void push_scope(JG* jg) {
    jg->scopes = (SymTab*)realloc(jg->scopes, (size_t)(jg->scope_count + 1) * sizeof(SymTab));
    memset(&jg->scopes[jg->scope_count], 0, sizeof(SymTab));
    jg->scope_count++;
}
static void pop_scope(JG* jg) {
    if (jg->scope_count <= 0) return;
    jg->scope_count--;
    symtab_free(&jg->scopes[jg->scope_count]);
}

static JType* lookup_var(JG* jg, const char* name) {
    for (int i = jg->scope_count - 1; i >= 0; i--) {
        JType* t = symtab_lookup(&jg->scopes[i], name);
        if (t) return jt_clone(t);
    }
    return NULL;
}

static JType* lookup_func_ret(JG* jg, const char* name) {
    for (int i = 0; i < jg->func_count; i++) {
        if (strcmp(jg->funcs[i].name, name) == 0) return jt_clone(jg->funcs[i].ret);
    }
    return NULL;
}

static JType* lookup_struct_field(JG* jg, const char* struct_name, const char* field) {
    for (int i = 0; i < jg->struct_count; i++) {
        if (strcmp(jg->structs[i].name, struct_name) == 0) {
            JType* t = symtab_lookup(&jg->structs[i].fields, field);
            return t ? jt_clone(t) : NULL;
        }
    }
    return NULL;
}

/* Génère un nom temporaire unique */
static void tmp_name(JG* jg, const char* prefix, char* buf, size_t n) {
    snprintf(buf, n, "%s%d", prefix, jg->tmp_id++);
}

/* AST -> Type Java */

static JType* ast_to_jtype(ASTNode* t) {
    if (!t) return jt_new(JT_UNKNOWN);

    if (t->kind == AST_TYPE_PRIMITIVE) {
        switch (t->as.type_prim.prim) {
            case TYPE_ENTIER: return jt_new(JT_INT);
            case TYPE_REEL: return jt_new(JT_DOUBLE);
            case TYPE_BOOLEEN: return jt_new(JT_BOOL);
            case TYPE_CARACTERE: return jt_new(JT_CHAR);
            case TYPE_CHAINE: return jt_new(JT_STRING);
            default: return jt_new(JT_UNKNOWN);
        }
    }

    if (t->kind == AST_TYPE_NAMED) {
        JType* j = jt_new(JT_STRUCT);
        j->struct_name = dupstr(t->as.type_named.name);
        return j;
    }

    if (t->kind == AST_TYPE_ARRAY) {
        JType* j = jt_new(JT_ARRAY);
        j->elem = ast_to_jtype(t->as.type_array.elem_type);
        j->dims = t->as.type_array.dims.count;
        return j;
    }

    return jt_new(JT_UNKNOWN);
}

static void emit_type_java(Str* out, JType* t) {
    if (!t) { str_append(out, "void"); return; }
    switch (t->kind) {
        case JT_INT: str_append(out, "int"); break;
        case JT_DOUBLE: str_append(out, "double"); break;
        case JT_BOOL: str_append(out, "boolean"); break;
        case JT_CHAR: str_append(out, "char"); break;
        case JT_STRING: str_append(out, "String"); break;
        case JT_STRUCT: str_append(out, t->struct_name ? t->struct_name : "Object"); break;
        case JT_ARRAY:
            emit_type_java(out, t->elem);
            for (int i = 0; i < t->dims; i++) str_append(out, "[]");
            break;
        default: str_append(out, "Object"); break;
    }
}

/* Inférence expr (Lire/Ecrire) */

static JType* infer_expr(JG* jg, ASTNode* e) {
    if (!e) return jt_new(JT_UNKNOWN);

    switch (e->kind) {
        case AST_LITERAL_INT: return jt_new(JT_INT);
        case AST_LITERAL_REAL: return jt_new(JT_DOUBLE);
        case AST_LITERAL_BOOL: return jt_new(JT_BOOL);
        case AST_LITERAL_STRING: return jt_new(JT_STRING);
        case AST_IDENT: {
            JType* t = lookup_var(jg, e->as.ident.name);
            return t ? t : jt_new(JT_UNKNOWN);
        }
        case AST_UNARY:
            if (e->as.unary.op == TOK_NON) return jt_new(JT_BOOL);
            return infer_expr(jg, e->as.unary.expr);
        case AST_BINARY: {
            TokenType op = e->as.binary.op;
            if ((op >= TOK_INFERIEUR && op <= TOK_DIFFERENT) || op == TOK_ET || op == TOK_OU) return jt_new(JT_BOOL);
            JType* l = infer_expr(jg, e->as.binary.lhs);
            JType* r = infer_expr(jg, e->as.binary.rhs);
            if (l->kind == JT_DOUBLE || r->kind == JT_DOUBLE || op == TOK_DIVISE) { jt_free(l); jt_free(r); return jt_new(JT_DOUBLE); }
            jt_free(l); jt_free(r);
            return jt_new(JT_INT);
        }
        case AST_CALL:
            if (e->as.call.callee && e->as.call.callee->kind == AST_IDENT) {
                JType* t = lookup_func_ret(jg, e->as.call.callee->as.ident.name);
                return t ? t : jt_new(JT_UNKNOWN);
            }
            return jt_new(JT_UNKNOWN);
        case AST_FIELD_ACCESS: {
            JType* b = infer_expr(jg, e->as.field_access.base);
            if (b && b->kind == JT_STRUCT && b->struct_name) {
                JType* f = lookup_struct_field(jg, b->struct_name, e->as.field_access.field);
                jt_free(b);
                return f ? f : jt_new(JT_UNKNOWN);
            }
            jt_free(b);
            return jt_new(JT_UNKNOWN);
        }
        case AST_INDEX: {
            JType* b = infer_expr(jg, e->as.index.base);
            if (b && b->kind == JT_ARRAY && b->elem) {
                if (b->dims > 1) {
                    JType* n = jt_clone(b);
                    n->dims--;
                    jt_free(b);
                    return n;
                } else {
                    JType* el = jt_clone(b->elem);
                    jt_free(b);
                    return el;
                }
            }
            jt_free(b);
            return jt_new(JT_UNKNOWN);
        }
        default:
            return jt_new(JT_UNKNOWN);
    }
}

/* Expressions Java */

static void emit_expr(JG* jg, ASTNode* e);

static void emit_binop(JG* jg, TokenType op) {
    switch (op) {
        case TOK_PLUS: str_append(&jg->out, " + "); break;
        case TOK_MOINS: str_append(&jg->out, " - "); break;
        case TOK_FOIS: str_append(&jg->out, " * "); break;
        case TOK_DIVISE: str_append(&jg->out, " / "); break;
        case TOK_DIV_ENTIER: str_append(&jg->out, " / "); break;
        case TOK_MODULO: str_append(&jg->out, " % "); break;
        case TOK_EGAL: str_append(&jg->out, " == "); break;
        case TOK_DIFFERENT: str_append(&jg->out, " != "); break;
        case TOK_INFERIEUR: str_append(&jg->out, " < "); break;
        case TOK_INFERIEUR_EGAL: str_append(&jg->out, " <= "); break;
        case TOK_SUPERIEUR: str_append(&jg->out, " > "); break;
        case TOK_SUPERIEUR_EGAL: str_append(&jg->out, " >= "); break;
        case TOK_ET: str_append(&jg->out, " && "); break;
        case TOK_OU: str_append(&jg->out, " || "); break;
        default: break;
    }
}

static void emit_string_literal(JG* jg, const char* s) {
    str_append(&jg->out, "\"");
    if (!s) { str_append(&jg->out, "\""); return; }
    for (const char* p = s; *p; p++) {
        if (*p == '\\') str_append(&jg->out, "\\\\");
        else if (*p == '"') str_append(&jg->out, "\\\"");
        else if (*p == '\n') str_append(&jg->out, "\\n");
        else if (*p == '\t') str_append(&jg->out, "\\t");
        else { char tmp[2] = {*p,0}; str_append(&jg->out, tmp); }
    }
    str_append(&jg->out, "\"");
}

static void emit_expr(JG* jg, ASTNode* e) {
    if (!e) { str_append(&jg->out, "null"); return; }

    switch (e->kind) {
        case AST_LITERAL_INT: str_printf(&jg->out, "%lld", e->as.lit_int.value); break;
        case AST_LITERAL_REAL: str_append(&jg->out, e->as.lit_real.text ? e->as.lit_real.text : "0.0"); break;
        case AST_LITERAL_BOOL: str_append(&jg->out, e->as.lit_bool.value ? "true" : "false"); break;
        case AST_LITERAL_STRING: emit_string_literal(jg, e->as.lit_string.text); break;
        case AST_IDENT: str_append(&jg->out, e->as.ident.name); break;

        case AST_UNARY:
            if (e->as.unary.op == TOK_NON) str_append(&jg->out, "!");
            else if (e->as.unary.op == TOK_MOINS) str_append(&jg->out, "-");
            str_append(&jg->out, "(");
            emit_expr(jg, e->as.unary.expr);
            str_append(&jg->out, ")");
            break;

        case AST_BINARY:
            if (e->as.binary.op == TOK_PUISSANCE) {
                str_append(&jg->out, "Math.pow(");
                emit_expr(jg, e->as.binary.lhs);
                str_append(&jg->out, ", ");
                emit_expr(jg, e->as.binary.rhs);
                str_append(&jg->out, ")");
            } else {
                str_append(&jg->out, "(");
                emit_expr(jg, e->as.binary.lhs);
                emit_binop(jg, e->as.binary.op);
                emit_expr(jg, e->as.binary.rhs);
                str_append(&jg->out, ")");
            }
            break;

        case AST_CALL:
            emit_expr(jg, e->as.call.callee);
            str_append(&jg->out, "(");
            for (int i = 0; i < e->as.call.args.count; i++) {
                if (i > 0) str_append(&jg->out, ", ");
                emit_expr(jg, e->as.call.args.items[i]);
            }
            str_append(&jg->out, ")");
            break;

        case AST_FIELD_ACCESS:
            emit_expr(jg, e->as.field_access.base);
            str_append(&jg->out, ".");
            str_append(&jg->out, e->as.field_access.field);
            break;

        case AST_INDEX:
            emit_expr(jg, e->as.index.base);
            str_append(&jg->out, "[");
            emit_expr(jg, e->as.index.index);
            str_append(&jg->out, "]");
            break;

        default:
            str_append(&jg->out, "null");
            break;
    }
}

/* Init tableaux de structs (Java) */

static void emit_struct_array_init_loops(JG* jg,
                                        const char* arr_name,
                                        const char* struct_name,
                                        ASTNode** dims,
                                        int dim_count) {
    if (!arr_name || !struct_name || !dims || dim_count <= 0) return;

    char idx[32];
    char idx_names[16][32];
    if (dim_count > 16) dim_count = 16;

    for (int k = 0; k < dim_count; k++) {
        tmp_name(jg, "_i", idx, sizeof(idx));
        snprintf(idx_names[k], sizeof(idx_names[k]), "%s", idx);

        emit_indent(jg);
        str_append(&jg->out, "for (int ");
        str_append(&jg->out, idx_names[k]);
        str_append(&jg->out, " = 0; ");
        str_append(&jg->out, idx_names[k]);
        str_append(&jg->out, " < (");
        emit_expr(jg, dims[k]);
        str_append(&jg->out, "); ");
        str_append(&jg->out, idx_names[k]);
        str_append(&jg->out, "++) {\n");
        jg->indent++;
    }

    emit_indent(jg);
    str_append(&jg->out, arr_name);
    for (int k = 0; k < dim_count; k++) {
        str_append(&jg->out, "[");
        str_append(&jg->out, idx_names[k]);
        str_append(&jg->out, "]");
    }
    str_append(&jg->out, " = new ");
    str_append(&jg->out, struct_name);
    str_append(&jg->out, "();\n");

    for (int k = dim_count - 1; k >= 0; k--) {
        jg->indent--;
        emit_ln(jg, "}");
    }
}

/* Statements Java */

static void emit_block(JG* jg, ASTNode* b);

static void record_global_struct_array_init(JG* jg, const char* name, const char* struct_name, ASTList* dims) {
    if (!jg || !name || !struct_name || !dims || dims->count <= 0) return;

    if (jg->g_arr_init_count >= jg->g_arr_init_cap) {
        jg->g_arr_init_cap = (jg->g_arr_init_cap == 0) ? 8 : jg->g_arr_init_cap * 2;
        jg->g_arr_inits = realloc(jg->g_arr_inits, (size_t)jg->g_arr_init_cap * sizeof(*jg->g_arr_inits));
    }

    int i = jg->g_arr_init_count++;
    jg->g_arr_inits[i].name = dupstr(name);
    jg->g_arr_inits[i].struct_name = dupstr(struct_name);
    jg->g_arr_inits[i].dim_count = dims->count;

    jg->g_arr_inits[i].dims = calloc((size_t)dims->count, sizeof(ASTNode*));
    for (int k = 0; k < dims->count; k++) jg->g_arr_inits[i].dims[k] = dims->items[k];
}

static void emit_decl(JG* jg, ASTNode* d, bool is_global) {
    if (!d) return;

    const char* name = NULL;
    ASTNode* typeNode = NULL;
    bool is_const = (d->kind == AST_DECL_CONST);

    if (d->kind == AST_DECL_VAR) { name = d->as.decl_var.name; typeNode = d->as.decl_var.type; }
    else if (d->kind == AST_DECL_CONST) { name = d->as.decl_const.name; typeNode = d->as.decl_const.type; }
    else if (d->kind == AST_DECL_ARRAY) { name = d->as.decl_array.name; typeNode = d->as.decl_array.elem_type; }
    else return;

    JType* t = ast_to_jtype(typeNode);
    if (d->kind == AST_DECL_ARRAY) {
        JType* arr = jt_new(JT_ARRAY);
        arr->elem = t;
        arr->dims = d->as.decl_array.dims.count;
        t = arr;
    }

    symtab_add(&jg->scopes[jg->scope_count - 1], name, t);

    emit_indent(jg);
    if (is_global) str_append(&jg->out, "static ");
    if (is_const) str_append(&jg->out, "final ");

    emit_type_java(&jg->out, t);
    str_printf(&jg->out, " %s", name);

    if (d->kind == AST_DECL_CONST) {
        str_append(&jg->out, " = ");
        emit_expr(jg, d->as.decl_const.value);
        str_append(&jg->out, ";\n");
        jt_free(t);
        return;
    }

    if (d->kind == AST_DECL_ARRAY) {
        str_append(&jg->out, " = new ");
        emit_type_java(&jg->out, t->elem);
        for (int i = 0; i < d->as.decl_array.dims.count; i++) {
            str_append(&jg->out, "[");
            emit_expr(jg, d->as.decl_array.dims.items[i]);
            str_append(&jg->out, "]");
        }
        str_append(&jg->out, ";\n");

        if (t->elem && t->elem->kind == JT_STRUCT && t->elem->struct_name) {
            if (is_global) {
                record_global_struct_array_init(jg, name, t->elem->struct_name, &d->as.decl_array.dims);
            } else {
                emit_struct_array_init_loops(jg, name, t->elem->struct_name,
                                             d->as.decl_array.dims.items,
                                             d->as.decl_array.dims.count);
            }
        }

        jt_free(t);
        return;
    }

    str_append(&jg->out, " = ");
    if (t->kind == JT_INT) str_append(&jg->out, "0");
    else if (t->kind == JT_DOUBLE) str_append(&jg->out, "0.0");
    else if (t->kind == JT_BOOL) str_append(&jg->out, "false");
    else if (t->kind == JT_CHAR) str_append(&jg->out, "'\\0'");
    else if (t->kind == JT_STRING) str_append(&jg->out, "\"\"");
    else if (t->kind == JT_STRUCT && t->struct_name) { str_append(&jg->out, "new "); str_append(&jg->out, t->struct_name); str_append(&jg->out, "()"); }
    else str_append(&jg->out, "null");
    str_append(&jg->out, ";\n");

    jt_free(t);
}

static void emit_write(JG* jg, ASTNode* s) {
    char sbname[32];
    tmp_name(jg, "_sb", sbname, sizeof(sbname));

    emit_ln(jg, "{");
    jg->indent++;
    emit_indent(jg);
    str_printf(&jg->out, "StringBuilder %s = new StringBuilder();\n", sbname);

    for (int i = 0; i < s->as.write_stmt.args.count; i++) {
        ASTNode* a = s->as.write_stmt.args.items[i];
        emit_indent(jg);
        str_printf(&jg->out, "%s.append(", sbname);
        if (a->kind == AST_LITERAL_STRING) {
            emit_expr(jg, a);
        } else {
            str_append(&jg->out, "String.valueOf(");
            emit_expr(jg, a);
            str_append(&jg->out, ")");
        }
        str_append(&jg->out, ");\n");
    }

    emit_indent(jg);
    str_printf(&jg->out, "System.out.println(%s.toString());\n", sbname);
    jg->indent--;
    emit_ln(jg, "}");
}

static void emit_read_one(JG* jg, ASTNode* target) {
    JType* t = infer_expr(jg, target);

    emit_indent(jg);
    emit_expr(jg, target);
    str_append(&jg->out, " = ");

    if (t->kind == JT_INT) str_append(&jg->out, "_sc.nextInt()");
    else if (t->kind == JT_DOUBLE) str_append(&jg->out, "_sc.nextDouble()");
    else if (t->kind == JT_BOOL) str_append(&jg->out, "_sc.nextBoolean()"); /* sans _readBool */
    else if (t->kind == JT_CHAR) str_append(&jg->out, "_sc.next().charAt(0)");
    else str_append(&jg->out, "_sc.next()");

    str_append(&jg->out, ";\n");
    jt_free(t);
}

static void emit_stmt(JG* jg, ASTNode* s) {
    if (!s) return;

    switch (s->kind) {
        case AST_ASSIGN:
            emit_indent(jg);
            emit_expr(jg, s->as.assign.target);
            str_append(&jg->out, " = ");
            emit_expr(jg, s->as.assign.value);
            str_append(&jg->out, ";\n");
            break;

        case AST_CALL_STMT:
            emit_indent(jg);
            emit_expr(jg, s->as.call_stmt.call);
            str_append(&jg->out, ";\n");
            break;

        case AST_RETURN:
            emit_indent(jg);
            str_append(&jg->out, "return");
            if (s->as.ret_stmt.value) {
                str_append(&jg->out, " ");
                emit_expr(jg, s->as.ret_stmt.value);
            }
            str_append(&jg->out, ";\n");
            break;

        case AST_WRITE:
            emit_write(jg, s);
            break;

        case AST_READ:
            for (int i = 0; i < s->as.read_stmt.targets.count; i++) emit_read_one(jg, s->as.read_stmt.targets.items[i]);
            break;

        case AST_IF: {
            emit_indent(jg);
            str_append(&jg->out, "if (");
            emit_expr(jg, s->as.if_stmt.cond);
            str_append(&jg->out, ") ");
            emit_block(jg, s->as.if_stmt.then_block);

            for (int i = 0; i < s->as.if_stmt.elif_conds.count; i++) {
                emit_indent(jg);
                str_append(&jg->out, "else if (");
                emit_expr(jg, s->as.if_stmt.elif_conds.items[i]);
                str_append(&jg->out, ") ");
                emit_block(jg, s->as.if_stmt.elif_blocks.items[i]);
            }

            if (s->as.if_stmt.else_block) {
                emit_indent(jg);
                str_append(&jg->out, "else ");
                emit_block(jg, s->as.if_stmt.else_block);
            }
            break;
        }

        case AST_WHILE:
            emit_indent(jg);
            str_append(&jg->out, "while (");
            emit_expr(jg, s->as.while_stmt.cond);
            str_append(&jg->out, ") ");
            emit_block(jg, s->as.while_stmt.body);
            break;

        case AST_REPEAT:
            emit_indent(jg);
            str_append(&jg->out, "do ");
            emit_block(jg, s->as.repeat_stmt.body);
            emit_indent(jg);
            str_append(&jg->out, "while (!(");
            if (s->as.repeat_stmt.until_cond) emit_expr(jg, s->as.repeat_stmt.until_cond);
            else str_append(&jg->out, "true");
            str_append(&jg->out, "));\n");
            break;

        case AST_FOR: {
            char stepname[32];
            tmp_name(jg, "_step", stepname, sizeof(stepname));

            emit_ln(jg, "{");
            jg->indent++;

            emit_indent(jg);
            str_printf(&jg->out, "int %s = ", stepname);
            if (s->as.for_stmt.step) emit_expr(jg, s->as.for_stmt.step);
            else str_append(&jg->out, "1");
            str_append(&jg->out, ";\n");

            emit_indent(jg);
            str_append(&jg->out, "for (");
            str_append(&jg->out, s->as.for_stmt.var);
            str_append(&jg->out, " = ");
            emit_expr(jg, s->as.for_stmt.start);
            str_append(&jg->out, "; ");

            str_append(&jg->out, "(");
            str_append(&jg->out, stepname);
            str_append(&jg->out, " >= 0) ? (");
            str_append(&jg->out, s->as.for_stmt.var);
            str_append(&jg->out, " <= ");
            emit_expr(jg, s->as.for_stmt.end);
            str_append(&jg->out, ") : (");
            str_append(&jg->out, s->as.for_stmt.var);
            str_append(&jg->out, " >= ");
            emit_expr(jg, s->as.for_stmt.end);
            str_append(&jg->out, "); ");

            str_append(&jg->out, s->as.for_stmt.var);
            str_append(&jg->out, " += ");
            str_append(&jg->out, stepname);
            str_append(&jg->out, ") ");

            emit_block(jg, s->as.for_stmt.body);

            jg->indent--;
            emit_ln(jg, "}");
            break;
        }

        case AST_SWITCH: {
            emit_indent(jg);
            str_append(&jg->out, "switch (");
            emit_expr(jg, s->as.switch_stmt.expr);
            str_append(&jg->out, ") {\n");
            jg->indent++;

            for (int i = 0; i < s->as.switch_stmt.cases.count; i++) {
                ASTNode* c = s->as.switch_stmt.cases.items[i];
                if (!c || c->kind != AST_CASE) continue;

                for (int j = 0; j < c->as.case_stmt.values.count; j++) {
                    emit_indent(jg);
                    str_append(&jg->out, "case ");
                    emit_expr(jg, c->as.case_stmt.values.items[j]);
                    str_append(&jg->out, ":\n");
                }

                emit_block(jg, c->as.case_stmt.body);
                emit_ln(jg, "break;");
            }

            if (s->as.switch_stmt.default_block) {
                emit_ln(jg, "default:");
                emit_block(jg, s->as.switch_stmt.default_block);
            }

            jg->indent--;
            emit_ln(jg, "}");
            break;
        }

        case AST_BREAK:
        case AST_QUIT_FOR:
            emit_ln(jg, "break;");
            break;

        default:
            break;
    }
}

static void emit_block(JG* jg, ASTNode* b) {
    str_append(&jg->out, "{\n");
    jg->indent++;
    push_scope(jg);

    if (b) {
        for (int i = 0; i < b->as.block.stmts.count; i++) {
            ASTNode* st = b->as.block.stmts.items[i];
            if (!st) continue;
            if (st->kind == AST_DECL_VAR || st->kind == AST_DECL_CONST || st->kind == AST_DECL_ARRAY) emit_decl(jg, st, false);
        }
        for (int i = 0; i < b->as.block.stmts.count; i++) {
            ASTNode* st = b->as.block.stmts.items[i];
            if (!st) continue;
            if (st->kind == AST_DECL_VAR || st->kind == AST_DECL_CONST || st->kind == AST_DECL_ARRAY) continue;
            emit_stmt(jg, st);
        }
    }

    pop_scope(jg);
    jg->indent--;
    emit_indent(jg);
    str_append(&jg->out, "}\n");
}

/* Structs / funcs / programme */

static void predeclare(JG* jg, ASTNode* program) {
    jg->funcs = calloc((size_t)program->as.program.defs.count, sizeof(*jg->funcs));
    jg->structs = calloc((size_t)program->as.program.defs.count, sizeof(*jg->structs));

    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;

        if (d->kind == AST_DEF_FUNC) {
            int k = jg->func_count++;
            jg->funcs[k].name = dupstr(d->as.def_func.name);
            jg->funcs[k].ret = ast_to_jtype(d->as.def_func.return_type);
        } else if (d->kind == AST_DEF_PROC) {
            int k = jg->func_count++;
            jg->funcs[k].name = dupstr(d->as.def_proc.name);
            jg->funcs[k].ret = jt_new(JT_UNKNOWN);
        }
    }
}

static void emit_structs(JG* jg, ASTNode* program) {
    bool has_structs = false;
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (d && d->kind == AST_DEF_STRUCT) { has_structs = true; break; }
    }
    if (!has_structs) return;

    emit_ln(jg, "// Structures");
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d || d->kind != AST_DEF_STRUCT) continue;

        int idx = jg->struct_count++;
        jg->structs[idx].name = dupstr(d->as.def_struct.name);

        emit_indent(jg);
        str_printf(&jg->out, "static class %s {\n", d->as.def_struct.name);
        jg->indent++;

        for (int j = 0; j < d->as.def_struct.fields.count; j++) {
            ASTNode* f = d->as.def_struct.fields.items[j];
            if (!f || f->kind != AST_FIELD) continue;

            JType* ft = ast_to_jtype(f->as.field.type);
            symtab_add(&jg->structs[idx].fields, f->as.field.name, ft);

            emit_indent(jg);
            emit_type_java(&jg->out, ft);
            str_printf(&jg->out, " %s;\n", f->as.field.name);

            jt_free(ft);
        }

        emit_indent(jg);
        str_printf(&jg->out, "%s() {\n", d->as.def_struct.name);
        jg->indent++;

        for (int j = 0; j < d->as.def_struct.fields.count; j++) {
            ASTNode* f = d->as.def_struct.fields.items[j];
            if (!f || f->kind != AST_FIELD) continue;

            JType* ft = ast_to_jtype(f->as.field.type);

            emit_indent(jg);
            str_printf(&jg->out, "this.%s = ", f->as.field.name);

            if (ft->kind == JT_INT) str_append(&jg->out, "0");
            else if (ft->kind == JT_DOUBLE) str_append(&jg->out, "0.0");
            else if (ft->kind == JT_BOOL) str_append(&jg->out, "false");
            else if (ft->kind == JT_CHAR) str_append(&jg->out, "'\\0'");
            else if (ft->kind == JT_STRING) str_append(&jg->out, "\"\"");
            else if (ft->kind == JT_STRUCT && ft->struct_name) { str_append(&jg->out, "new "); str_append(&jg->out, ft->struct_name); str_append(&jg->out, "()"); }
            else str_append(&jg->out, "null");

            str_append(&jg->out, ";\n");
            jt_free(ft);
        }

        jg->indent--;
        emit_ln(jg, "}");

        jg->indent--;
        emit_ln(jg, "}");
        emit_ln(jg, "");
    }
}

static void emit_funcproc(JG* jg, ASTNode* def) {
    bool isFunc = (def->kind == AST_DEF_FUNC);
    const char* name = isFunc ? def->as.def_func.name : def->as.def_proc.name;
    ASTList* params = isFunc ? &def->as.def_func.params : &def->as.def_proc.params;
    ASTNode* body = isFunc ? def->as.def_func.body : def->as.def_proc.body;

    jg->tmp_id = 0;

    emit_indent(jg);
    str_append(&jg->out, "static ");

    if (isFunc) {
        JType* rt = ast_to_jtype(def->as.def_func.return_type);
        emit_type_java(&jg->out, rt);
        jt_free(rt);
    } else {
        str_append(&jg->out, "void");
    }

    str_printf(&jg->out, " %s(", name);

    push_scope(jg);

    for (int i = 0; i < params->count; i++) {
        if (i > 0) str_append(&jg->out, ", ");
        ASTNode* p = params->items[i];
        JType* pt = ast_to_jtype(p->as.param.type);
        symtab_add(&jg->scopes[jg->scope_count - 1], p->as.param.name, pt);
        emit_type_java(&jg->out, pt);
        str_printf(&jg->out, " %s", p->as.param.name);
        jt_free(pt);
    }

    str_append(&jg->out, ") ");
    emit_block(jg, body);

    pop_scope(jg);
    emit_ln(jg, "");
}

static void emit_global_static_init(JG* jg) {
    if (jg->g_arr_init_count <= 0) return;

    jg->tmp_id = 0;

    emit_ln(jg, "static {");
    jg->indent++;

    for (int i = 0; i < jg->g_arr_init_count; i++) {
        emit_struct_array_init_loops(jg,
                                     jg->g_arr_inits[i].name,
                                     jg->g_arr_inits[i].struct_name,
                                     jg->g_arr_inits[i].dims,
                                     jg->g_arr_inits[i].dim_count);
    }

    jg->indent--;
    emit_ln(jg, "}");
    emit_ln(jg, "");
}

bool jgen_generate(ASTNode* program, const char* out_path) {
    if (!program || program->kind != AST_PROGRAM) return false;

    JG jg;
    memset(&jg, 0, sizeof(jg));
    str_init(&jg.out);
    jg.indent = 0;
    jg.class_name = "Main";
    jg.tmp_id = 0;

    push_scope(&jg);
    predeclare(&jg, program);

    emit_ln(&jg, "import java.util.*;");
    emit_ln(&jg, "");
    str_printf(&jg.out, "public class %s {\n", jg.class_name);

    jg.indent = 1;

    /* Scanner */
    emit_ln(&jg, "static Scanner _sc = new Scanner(System.in);");
    emit_ln(&jg, "");

    emit_structs(&jg, program);

    emit_ln(&jg, "// Globales");
    for (int i = 0; i < program->as.program.decls.count; i++) {
        ASTNode* d = program->as.program.decls.items[i];
        if (!d) continue;
        emit_decl(&jg, d, true);
    }
    emit_ln(&jg, "");

    emit_global_static_init(&jg);

    emit_ln(&jg, "// Fonctions / Procédures");
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;
        if (d->kind == AST_DEF_FUNC || d->kind == AST_DEF_PROC) emit_funcproc(&jg, d);
    }

    emit_ln(&jg, "public static void main(String[] args) {");
    jg.indent++;
    push_scope(&jg);

    jg.tmp_id = 0;

    ASTNode* mb = program->as.program.main_block;
    if (mb) {
        for (int i = 0; i < mb->as.block.stmts.count; i++) {
            ASTNode* st = mb->as.block.stmts.items[i];
            if (!st) continue;
            if (st->kind == AST_DECL_VAR || st->kind == AST_DECL_CONST || st->kind == AST_DECL_ARRAY) emit_decl(&jg, st, false);
        }
        for (int i = 0; i < mb->as.block.stmts.count; i++) {
            ASTNode* st = mb->as.block.stmts.items[i];
            if (!st) continue;
            if (st->kind == AST_DECL_VAR || st->kind == AST_DECL_CONST || st->kind == AST_DECL_ARRAY) continue;
            emit_stmt(&jg, st);
        }
    }

    pop_scope(&jg);
    jg.indent--;
    emit_ln(&jg, "}");

    str_append(&jg.out, "}\n");

    FILE* f = fopen(out_path, "w");
    if (f) {
        fputs(jg.out.data ? jg.out.data : "", f);
        fclose(f);
    }

    str_free(&jg.out);

    for (int i = 0; i < jg.struct_count; i++) {
        free(jg.structs[i].name);
        symtab_free(&jg.structs[i].fields);
    }
    for (int i = 0; i < jg.func_count; i++) {
        free(jg.funcs[i].name);
        jt_free(jg.funcs[i].ret);
    }
    free(jg.structs);
    free(jg.funcs);

    for (int i = 0; i < jg.g_arr_init_count; i++) {
        free(jg.g_arr_inits[i].name);
        free(jg.g_arr_inits[i].struct_name);
        free(jg.g_arr_inits[i].dims);
    }
    free(jg.g_arr_inits);

    while (jg.scope_count > 0) pop_scope(&jg);
    free(jg.scopes);

    return true;
}
