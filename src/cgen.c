#include "cgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

// Utilitaires de chaînes

static char* cgen_strdup(const char* s) {
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

static void str_init(Str* s) {
    s->data = NULL; s->len = 0; s->cap = 0;
}

static void str_append(Str* s, const char* t) {
    if (!t) return;
    size_t n = strlen(t);
    if (s->len + n + 1 > s->cap) {
        size_t new_cap = (s->cap == 0) ? 1024 : s->cap * 2;
        while (new_cap < s->len + n + 1) new_cap *= 2;
        s->data = realloc(s->data, new_cap);
        s->cap = new_cap;
    }
    memcpy(s->data + s->len, t, n);
    s->len += n;
    s->data[s->len] = '\0';
}

static void str_printf(Str* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str_append(s, buf);
}

static void str_free(Str* s) {
    free(s->data);
}

// Système de Types (C-Type)

typedef enum { CT_UNKNOWN, CT_INT, CT_REAL, CT_BOOL, CT_CHAR, CT_STRING, CT_STRUCT, CT_ARRAY } CTypeKind;

typedef struct CType {
    CTypeKind kind;
    char* struct_name;
    struct CType* elem;
    int dims;
} CType;

static CType* ct_new(CTypeKind k) {
    CType* t = calloc(1, sizeof(CType));
    t->kind = k;
    return t;
}

static CType* ct_clone(const CType* src) {
    if (!src) return ct_new(CT_UNKNOWN);
    CType* t = ct_new(src->kind);
    if (src->struct_name) t->struct_name = cgen_strdup(src->struct_name);
    if (src->elem) t->elem = ct_clone(src->elem);
    t->dims = src->dims;
    return t;
}

static void ct_free(CType* t) {
    if (!t) return;
    free(t->struct_name);
    ct_free(t->elem);
    free(t);
}

// Tables des Symboles

typedef struct { char* name; CType* type; } Sym;
typedef struct { Sym* items; int count; int cap; } SymTab;

static void symtab_add(SymTab* st, const char* name, CType* type) {
    if (st->count >= st->cap) {
        st->cap = (st->cap == 0) ? 16 : st->cap * 2;
        st->items = realloc(st->items, st->cap * sizeof(Sym));
    }
    st->items[st->count].name = cgen_strdup(name);
    st->items[st->count].type = ct_clone(type);
    st->count++;
}

static CType* symtab_lookup(SymTab* st, const char* name) {
    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->items[i].name, name) == 0) return st->items[i].type;
    }
    return NULL;
}

static void symtab_free(SymTab* st) {
    for (int i=0; i<st->count; i++) { free(st->items[i].name); ct_free(st->items[i].type); }
    free(st->items);
}

typedef struct {
    Str out;
    int indent;
    struct { char* name; SymTab fields; }* structs;
    int struct_count;
    struct { char* name; CType* ret; }* funcs;
    int func_count;
    SymTab* scopes;
    int scope_count;
} CG;

// Helpers

static void emit_indent(CG* cg) { for(int i=0; i<cg->indent; i++) str_append(&cg->out, "    "); }
static void emit_ln(CG* cg, const char* s) { emit_indent(cg); str_append(&cg->out, s); str_append(&cg->out, "\n"); }

static void push_scope(CG* cg) {
    cg->scopes = realloc(cg->scopes, (cg->scope_count + 1) * sizeof(SymTab));
    memset(&cg->scopes[cg->scope_count], 0, sizeof(SymTab));
    cg->scope_count++;
}

static void pop_scope(CG* cg) {
    cg->scope_count--;
    symtab_free(&cg->scopes[cg->scope_count]);
}

static CType* lookup_var(CG* cg, const char* name) {
    for (int i = cg->scope_count - 1; i >= 0; i--) {
        CType* t = symtab_lookup(&cg->scopes[i], name);
        if (t) return ct_clone(t);
    }
    return NULL;
}

static CType* lookup_func_ret(CG* cg, const char* name) {
    for (int i=0; i<cg->func_count; i++) {
        if (strcmp(cg->funcs[i].name, name) == 0) return ct_clone(cg->funcs[i].ret);
    }
    return NULL;
}

static CType* lookup_struct_field(CG* cg, const char* struct_name, const char* field) {
    for (int i=0; i<cg->struct_count; i++) {
        if (strcmp(cg->structs[i].name, struct_name) == 0) {
            return ct_clone(symtab_lookup(&cg->structs[i].fields, field));
        }
    }
    return NULL;
}

static CType* ast_to_ctype(ASTNode* t) {
    if (!t) return ct_new(CT_UNKNOWN);
    if (t->kind == AST_TYPE_PRIMITIVE) {
        switch (t->as.type_prim.prim) {
            case TYPE_ENTIER: return ct_new(CT_INT);
            case TYPE_REEL: return ct_new(CT_REAL);
            case TYPE_BOOLEEN: return ct_new(CT_BOOL);
            case TYPE_CARACTERE: return ct_new(CT_CHAR);
            case TYPE_CHAINE: return ct_new(CT_STRING);
            default: return ct_new(CT_UNKNOWN);
        }
    }
    if (t->kind == AST_TYPE_NAMED) {
        CType* c = ct_new(CT_STRUCT);
        c->struct_name = cgen_strdup(t->as.type_named.name);
        return c;
    }
    if (t->kind == AST_TYPE_ARRAY) {
        CType* c = ct_new(CT_ARRAY);
        c->elem = ast_to_ctype(t->as.type_array.elem_type);
        c->dims = t->as.type_array.dims.count;
        return c;
    }
    return ct_new(CT_UNKNOWN);
}

static void emit_type_str(Str* out, CType* t) {
    if (!t) { str_append(out, "void"); return; }
    switch (t->kind) {
        case CT_INT: str_append(out, "int"); break;
        case CT_REAL: str_append(out, "double"); break;
        case CT_BOOL: str_append(out, "bool"); break;
        case CT_CHAR: str_append(out, "char"); break;
        case CT_STRING: str_append(out, "char*"); break;
        case CT_STRUCT: str_append(out, t->struct_name); break;
        case CT_ARRAY: emit_type_str(out, t->elem); break;
        default: str_append(out, "void"); break;
    }
}

// Type Inference

static CType* infer_expr(CG* cg, ASTNode* e) {
    if (!e) return ct_new(CT_UNKNOWN);
    switch (e->kind) {
        case AST_LITERAL_INT: return ct_new(CT_INT);
        case AST_LITERAL_REAL: return ct_new(CT_REAL);
        case AST_LITERAL_BOOL: return ct_new(CT_BOOL);
        case AST_LITERAL_STRING: return ct_new(CT_STRING);
        case AST_IDENT: return lookup_var(cg, e->as.ident.name);
        case AST_BINARY: {
            CType* l = infer_expr(cg, e->as.binary.lhs);
            CType* r = infer_expr(cg, e->as.binary.rhs);
            TokenType op = e->as.binary.op;
            
            if ((op >= TOK_INFERIEUR && op <= TOK_DIFFERENT) || op == TOK_ET || op == TOK_OU || op == TOK_NON) {
                ct_free(l); ct_free(r); return ct_new(CT_BOOL);
            }
            if (op == TOK_DIVISE) { ct_free(l); ct_free(r); return ct_new(CT_REAL); }
            
            if (l->kind == CT_REAL || r->kind == CT_REAL) { ct_free(l); ct_free(r); return ct_new(CT_REAL); }
            ct_free(l); ct_free(r); return ct_new(CT_INT);
        }
        case AST_UNARY: return (e->as.unary.op == TOK_NON) ? ct_new(CT_BOOL) : infer_expr(cg, e->as.unary.expr);
        case AST_CALL:
            if (e->as.call.callee->kind == AST_IDENT) return lookup_func_ret(cg, e->as.call.callee->as.ident.name);
            return ct_new(CT_UNKNOWN);
        case AST_FIELD_ACCESS: {
            CType* base = infer_expr(cg, e->as.field_access.base);
            if (base && base->kind == CT_STRUCT) {
                CType* ret = lookup_struct_field(cg, base->struct_name, e->as.field_access.field);
                ct_free(base); return ret ? ret : ct_new(CT_UNKNOWN);
            }
            ct_free(base); return ct_new(CT_UNKNOWN);
        }
        case AST_INDEX: {
            CType* base = infer_expr(cg, e->as.index.base);
            if (base && base->kind == CT_ARRAY) {
                if (base->dims > 1) {
                    CType* next = ct_clone(base);
                    next->dims--;
                    ct_free(base); return next;
                } else {
                    CType* elem = ct_clone(base->elem);
                    ct_free(base); return elem;
                }
            }
            ct_free(base); return ct_new(CT_UNKNOWN);
        }
        default: return ct_new(CT_UNKNOWN);
    }
}

// Émission de Code

static void emit_expr(CG* cg, ASTNode* e);

static void emit_op(CG* cg, TokenType op) {
    switch (op) {
        case TOK_PLUS: str_append(&cg->out, " + "); break;
        case TOK_MOINS: str_append(&cg->out, " - "); break;
        case TOK_FOIS: str_append(&cg->out, " * "); break;
        case TOK_DIVISE: str_append(&cg->out, " / "); break;
        case TOK_DIV_ENTIER: str_append(&cg->out, " / "); break;
        case TOK_MODULO: str_append(&cg->out, " % "); break;
        case TOK_EGAL: str_append(&cg->out, " == "); break;
        case TOK_DIFFERENT: str_append(&cg->out, " != "); break;
        case TOK_INFERIEUR: str_append(&cg->out, " < "); break;
        case TOK_INFERIEUR_EGAL: str_append(&cg->out, " <= "); break;
        case TOK_SUPERIEUR: str_append(&cg->out, " > "); break;
        case TOK_SUPERIEUR_EGAL: str_append(&cg->out, " >= "); break;
        case TOK_ET: str_append(&cg->out, " && "); break;
        case TOK_OU: str_append(&cg->out, " || "); break;
        default: break;
    }
}

static bool try_emit_flat_index(CG* cg, ASTNode* idx) {
    if (idx->kind != AST_INDEX) return false;
    ASTNode* base = idx->as.index.base;
    if (base->kind == AST_INDEX && base->as.index.base->kind == AST_IDENT) {
        CType* t = lookup_var(cg, base->as.index.base->as.ident.name);
        if (t && t->kind == CT_ARRAY && t->dims > 1) {
            CType* tm = lookup_var(cg, "m"); 
            if (tm) {
                str_append(&cg->out, base->as.index.base->as.ident.name);
                str_append(&cg->out, "[("); emit_expr(cg, base->as.index.index);
                str_append(&cg->out, ") * m + ("); emit_expr(cg, idx->as.index.index);
                str_append(&cg->out, ")]");
                ct_free(tm); ct_free(t); return true;
            }
            if (tm) ct_free(tm);
        }
        if (t) ct_free(t);
    }
    return false;
}

static void emit_expr(CG* cg, ASTNode* e) {
    if (!e) return;
    switch (e->kind) {
        case AST_LITERAL_INT: str_printf(&cg->out, "%lld", e->as.lit_int.value); break;
        case AST_LITERAL_REAL: str_append(&cg->out, e->as.lit_real.text); break;
        case AST_LITERAL_BOOL: str_append(&cg->out, e->as.lit_bool.value ? "true" : "false"); break;
        case AST_LITERAL_STRING: 
            str_append(&cg->out, "\"");
            for(char* p=e->as.lit_string.text; *p; p++) {
                if (*p == '"') str_append(&cg->out, "\\\"");
                else if (*p == '\\') str_append(&cg->out, "\\\\");
                else { char tmp[2]={*p,0}; str_append(&cg->out, tmp); }
            }
            str_append(&cg->out, "\"");
            break;
        case AST_IDENT: str_append(&cg->out, e->as.ident.name); break;
        case AST_BINARY:
            if (e->as.binary.op == TOK_DIVISE) {
                str_append(&cg->out, "((double)(");
                emit_expr(cg, e->as.binary.lhs);
                str_append(&cg->out, ")) / (");
                emit_expr(cg, e->as.binary.rhs);
                str_append(&cg->out, ")");
            } else {
                str_append(&cg->out, "("); emit_expr(cg, e->as.binary.lhs);
                emit_op(cg, e->as.binary.op); emit_expr(cg, e->as.binary.rhs); str_append(&cg->out, ")");
            }
            break;
        case AST_UNARY:
            if (e->as.unary.op == TOK_NON) str_append(&cg->out, "!"); else str_append(&cg->out, "-");
            str_append(&cg->out, "("); emit_expr(cg, e->as.unary.expr); str_append(&cg->out, ")");
            break;
        case AST_CALL:
            emit_expr(cg, e->as.call.callee);
            str_append(&cg->out, "(");
            for (int i=0; i<e->as.call.args.count; i++) {
                if (i>0) str_append(&cg->out, ", ");
                ASTNode* arg = e->as.call.args.items[i];
                if (arg->kind == AST_IDENT) {
                    CType* t = lookup_var(cg, arg->as.ident.name);
                    if (t && t->kind == CT_ARRAY && t->dims > 1) str_append(&cg->out, "(int*)");
                    if (t) ct_free(t);
                }
                emit_expr(cg, arg);
            }
            str_append(&cg->out, ")");
            break;
        case AST_FIELD_ACCESS:
            emit_expr(cg, e->as.field_access.base); str_append(&cg->out, "."); str_append(&cg->out, e->as.field_access.field);
            break;
        case AST_INDEX:
            if (!try_emit_flat_index(cg, e)) {
                emit_expr(cg, e->as.index.base); str_append(&cg->out, "["); emit_expr(cg, e->as.index.index); str_append(&cg->out, "]");
            }
            break;
        default: break;
    }
}

// Statements

static void emit_block(CG* cg, ASTNode* b);

static void emit_decl(CG* cg, ASTNode* d, bool is_global) {
    if (!d) return;
    
    // Ignore les commentaires
    if (d->kind == 0 && d->line == 0 && d->col == 0) return; // Fallback

    const char* name = (d->kind == AST_DECL_VAR) ? d->as.decl_var.name : 
                       (d->kind == AST_DECL_CONST) ? d->as.decl_const.name : d->as.decl_array.name;
    ASTNode* typeNode = (d->kind == AST_DECL_VAR) ? d->as.decl_var.type :
                        (d->kind == AST_DECL_CONST) ? d->as.decl_const.type : d->as.decl_array.elem_type;
    CType* ct = ast_to_ctype(typeNode);
    if (d->kind == AST_DECL_ARRAY) {
        CType* arr = ct_new(CT_ARRAY); arr->elem = ct; arr->dims = d->as.decl_array.dims.count; ct = arr;
    }
    symtab_add(&cg->scopes[cg->scope_count-1], name, ct);
    if (is_global && d->kind == AST_DECL_CONST && ct->kind == CT_INT) { ct_free(ct); return; }
    
    emit_indent(cg);
    if (d->kind == AST_DECL_CONST && ct->kind != CT_INT) str_append(&cg->out, "const ");
    emit_type_str(&cg->out, ct); str_printf(&cg->out, " %s", name);
    
    if (d->kind == AST_DECL_ARRAY) {
        for(int i=0; i<d->as.decl_array.dims.count; i++) {
            str_append(&cg->out, "["); emit_expr(cg, d->as.decl_array.dims.items[i]); str_append(&cg->out, "]");
        }
    } else if (d->kind == AST_DECL_CONST) {
        str_append(&cg->out, " = "); emit_expr(cg, d->as.decl_const.value);
    } else if (ct->kind == CT_STRING) {
        str_append(&cg->out, " = NULL");
    }
    str_append(&cg->out, ";\n");
    ct_free(ct);
}

static void emit_stmt(CG* cg, ASTNode* s) {
    if (!s) return;
    switch (s->kind) {
        case AST_ASSIGN:
            emit_indent(cg); emit_expr(cg, s->as.assign.target); str_append(&cg->out, " = ");
            emit_expr(cg, s->as.assign.value);
            str_append(&cg->out, ";\n");
            break;
        case AST_IF:
            emit_indent(cg); str_append(&cg->out, "if ("); emit_expr(cg, s->as.if_stmt.cond); str_append(&cg->out, ") ");
            emit_block(cg, s->as.if_stmt.then_block);
            if (s->as.if_stmt.else_block) { emit_indent(cg); str_append(&cg->out, "else "); emit_block(cg, s->as.if_stmt.else_block); }
            break;
        case AST_WHILE:
            emit_indent(cg); str_append(&cg->out, "while ("); emit_expr(cg, s->as.while_stmt.cond); str_append(&cg->out, ") ");
            emit_block(cg, s->as.while_stmt.body);
            break;
        case AST_REPEAT: 
            emit_indent(cg); str_append(&cg->out, "do ");
            emit_block(cg, s->as.repeat_stmt.body);
            emit_indent(cg); str_append(&cg->out, "while (");
            if (s->as.repeat_stmt.until_cond) emit_expr(cg, s->as.repeat_stmt.until_cond);
            else str_append(&cg->out, "1");
            str_append(&cg->out, ");\n");
            break;
        case AST_FOR:
            emit_indent(cg); str_printf(&cg->out, "for (%s = ", s->as.for_stmt.var);
            emit_expr(cg, s->as.for_stmt.start); str_printf(&cg->out, "; %s <= ", s->as.for_stmt.var);
            emit_expr(cg, s->as.for_stmt.end); str_printf(&cg->out, "; %s++) ", s->as.for_stmt.var);
            emit_block(cg, s->as.for_stmt.body);
            break;
        case AST_RETURN:
            emit_indent(cg); str_append(&cg->out, "return");
            if (s->as.ret_stmt.value) { str_append(&cg->out, " "); emit_expr(cg, s->as.ret_stmt.value); }
            str_append(&cg->out, ";\n");
            break;
        case AST_WRITE: {
            emit_indent(cg); str_append(&cg->out, "printf(\"");
            for(int i=0; i<s->as.write_stmt.args.count; i++) {
                ASTNode* arg = s->as.write_stmt.args.items[i];
                if (arg->kind == AST_LITERAL_STRING) str_append(&cg->out, arg->as.lit_string.text);
                else {
                    CType* t = infer_expr(cg, arg);
                    if (t->kind == CT_INT || t->kind == CT_BOOL) str_append(&cg->out, "%d");
                    else if (t->kind == CT_REAL) str_append(&cg->out, "%g");
                    else if (t->kind == CT_CHAR) str_append(&cg->out, "%c");
                    else str_append(&cg->out, "%s");
                    ct_free(t);
                }
            }
            str_append(&cg->out, "\\n\"");
            for(int i=0; i<s->as.write_stmt.args.count; i++) {
                ASTNode* arg = s->as.write_stmt.args.items[i];
                if (arg->kind != AST_LITERAL_STRING) { 
                    str_append(&cg->out, ", "); 
                    emit_expr(cg, arg);
                }
            }
            str_append(&cg->out, ");\n");
            break;
        }
        case AST_READ:
             for(int i=0; i<s->as.read_stmt.targets.count; i++) {
                ASTNode* target = s->as.read_stmt.targets.items[i];
                CType* t = infer_expr(cg, target);
                emit_indent(cg); 
                if (t->kind == CT_STRING) { 
                    str_append(&cg->out, "{ "); emit_expr(cg, target); str_append(&cg->out, " = malloc(256); scanf(\"%s\", "); emit_expr(cg, target); str_append(&cg->out, "); }\n"); 
                }
                else {
                    str_printf(&cg->out, "scanf(\"%s\", &", (t->kind == CT_REAL) ? "%lf" : (t->kind == CT_CHAR) ? " %c" : "%d");
                    emit_expr(cg, target); str_append(&cg->out, ");\n");
                }
                ct_free(t);
             }
             break;
        case AST_SWITCH:
            emit_indent(cg); str_append(&cg->out, "switch ("); emit_expr(cg, s->as.switch_stmt.expr); str_append(&cg->out, ") {\n");
            for(int i=0; i<s->as.switch_stmt.cases.count; i++) {
                ASTNode* c = s->as.switch_stmt.cases.items[i];
                for(int j=0; j<c->as.case_stmt.values.count; j++) {
                    emit_indent(cg); str_append(&cg->out, "case "); emit_expr(cg, c->as.case_stmt.values.items[j]); str_append(&cg->out, ":\n");
                }
                emit_block(cg, c->as.case_stmt.body); emit_ln(cg, "break;");
            }
            if (s->as.switch_stmt.default_block) { emit_ln(cg, "default:"); emit_block(cg, s->as.switch_stmt.default_block); }
            emit_indent(cg); str_append(&cg->out, "}\n");
            break;
        case AST_BREAK: case AST_QUIT_FOR: emit_ln(cg, "break;"); break;
        case AST_CALL_STMT: emit_indent(cg); emit_expr(cg, s->as.call_stmt.call); str_append(&cg->out, ";\n"); break;
        default: break;
    }
}

static void emit_block(CG* cg, ASTNode* b) {
    if (!b) return;
    str_append(&cg->out, "{\n"); cg->indent++; push_scope(cg);
    for(int i=0; i<b->as.block.stmts.count; i++) {
        ASTNode* s = b->as.block.stmts.items[i];
        if (s->kind == AST_DECL_VAR || s->kind == AST_DECL_CONST || s->kind == AST_DECL_ARRAY) emit_decl(cg, s, false);
    }
    for(int i=0; i<b->as.block.stmts.count; i++) {
        ASTNode* s = b->as.block.stmts.items[i];
        if (s->kind != AST_DECL_VAR && s->kind != AST_DECL_CONST && s->kind != AST_DECL_ARRAY) emit_stmt(cg, s);
    }
    pop_scope(cg); cg->indent--; emit_indent(cg); str_append(&cg->out, "}\n");
}

bool cgen_generate(ASTNode* program, const char* output_c_path) {
    if (!program) return false;
    CG cg; memset(&cg, 0, sizeof(cg)); str_init(&cg.out); push_scope(&cg);
    
    // Headers standards UNIQUEMENT
    emit_ln(&cg, "#include <stdio.h>");
    emit_ln(&cg, "#include <stdlib.h>");
    emit_ln(&cg, "#include <stdbool.h>");
    emit_ln(&cg, "#include <string.h>");
    emit_ln(&cg, "#include <math.h>");
    emit_ln(&cg, "");

    cg.structs = calloc(program->as.program.defs.count, sizeof(*cg.structs));
    cg.funcs = calloc(program->as.program.defs.count, sizeof(*cg.funcs));
    
    bool has_structs = false;
    for (int i=0; i<program->as.program.defs.count; i++) {
        if (program->as.program.defs.items[i]->kind == AST_DEF_STRUCT) { has_structs = true; break; }
    }

    if (has_structs) emit_ln(&cg, "// Structures");
    
    for (int i=0; i<program->as.program.defs.count; i++) {
        ASTNode* def = program->as.program.defs.items[i];
        if (def->kind == AST_DEF_STRUCT) {
            int idx = cg.struct_count++; cg.structs[idx].name = cgen_strdup(def->as.def_struct.name);
            for(int j=0; j<def->as.def_struct.fields.count; j++) {
                ASTNode* f = def->as.def_struct.fields.items[j];
                symtab_add(&cg.structs[idx].fields, f->as.field.name, ast_to_ctype(f->as.field.type));
            }
            str_printf(&cg.out, "typedef struct %s {\n", def->as.def_struct.name); cg.indent++;
            for(int j=0; j<def->as.def_struct.fields.count; j++) {
                ASTNode* f = def->as.def_struct.fields.items[j];
                emit_indent(&cg); emit_type_str(&cg.out, ast_to_ctype(f->as.field.type)); str_printf(&cg.out, " %s;\n", f->as.field.name);
            }
            cg.indent--; str_printf(&cg.out, "} %s;\n\n", def->as.def_struct.name);
        } else if (def->kind == AST_DEF_FUNC || def->kind == AST_DEF_PROC) {
            int idx = cg.func_count++;
            cg.funcs[idx].name = cgen_strdup(def->kind == AST_DEF_FUNC ? def->as.def_func.name : def->as.def_proc.name);
            cg.funcs[idx].ret = (def->kind == AST_DEF_FUNC) ? ast_to_ctype(def->as.def_func.return_type) : ct_new(CT_UNKNOWN);
        }
    }

    bool has_const_int = false;
    for(int i=0; i<program->as.program.decls.count; i++) {
        ASTNode* d = program->as.program.decls.items[i];
        if (d->kind == AST_DECL_CONST) {
            CType* t = ast_to_ctype(d->as.decl_const.type);
            if (t->kind == CT_INT) has_const_int = true;
            ct_free(t);
        }
    }

    if (has_const_int) {
        emit_ln(&cg, "// Constantes");
        emit_ln(&cg, "enum {"); cg.indent++;
        for(int i=0; i<program->as.program.decls.count; i++) {
            ASTNode* d = program->as.program.decls.items[i];
            if (d->kind == AST_DECL_CONST) {
                CType* t = ast_to_ctype(d->as.decl_const.type);
                if (t->kind == CT_INT) { emit_indent(&cg); str_append(&cg.out, d->as.decl_const.name); str_append(&cg.out, " = "); emit_expr(&cg, d->as.decl_const.value); str_append(&cg.out, ",\n"); }
                ct_free(t);
            }
        }
        cg.indent--; emit_ln(&cg, "};\n");
    }

    emit_ln(&cg, "// Globales");
    for(int i=0; i<program->as.program.decls.count; i++) emit_decl(&cg, program->as.program.decls.items[i], true);
    emit_ln(&cg, "");

    bool has_funcs = false;
    for (int i=0; i<program->as.program.defs.count; i++) {
        if (program->as.program.defs.items[i]->kind == AST_DEF_FUNC || program->as.program.defs.items[i]->kind == AST_DEF_PROC) { has_funcs = true; break; }
    }

    if (has_funcs) emit_ln(&cg, "// Fonctions");
    for (int i=0; i<program->as.program.defs.count; i++) {
        ASTNode* def = program->as.program.defs.items[i];
        if (def->kind == AST_DEF_FUNC || def->kind == AST_DEF_PROC) {
            bool isFunc = (def->kind == AST_DEF_FUNC);
            CType* ret = isFunc ? ast_to_ctype(def->as.def_func.return_type) : NULL;
            emit_type_str(&cg.out, ret);
            str_printf(&cg.out, " %s(", isFunc ? def->as.def_func.name : def->as.def_proc.name);
            push_scope(&cg);
            ASTList* params = isFunc ? &def->as.def_func.params : &def->as.def_proc.params;
            for(int p=0; p<params->count; p++) {
                if (p>0) str_append(&cg.out, ", ");
                ASTNode* pm = params->items[p]; CType* pt = ast_to_ctype(pm->as.param.type);
                symtab_add(&cg.scopes[cg.scope_count-1], pm->as.param.name, pt);
                emit_type_str(&cg.out, pt);
                if (pt->kind == CT_ARRAY) str_printf(&cg.out, " %s[]", pm->as.param.name);
                else str_printf(&cg.out, " %s", pm->as.param.name);
                ct_free(pt);
            }
            str_append(&cg.out, ") ");
            emit_block(&cg, isFunc ? def->as.def_func.body : def->as.def_proc.body);
            pop_scope(&cg); ct_free(ret); emit_ln(&cg, "");
        }
    }

    emit_ln(&cg, "// Main");
    emit_ln(&cg, "int main(void) {"); cg.indent++; push_scope(&cg);
    ASTNode* mb = program->as.program.main_block;
    if (mb) {
        for(int i=0; i<mb->as.block.stmts.count; i++) {
            ASTNode* s = mb->as.block.stmts.items[i];
            if (s->kind == AST_DECL_VAR || s->kind == AST_DECL_CONST || s->kind == AST_DECL_ARRAY) emit_decl(&cg, s, false);
        }
        for(int i=0; i<mb->as.block.stmts.count; i++) {
            ASTNode* s = mb->as.block.stmts.items[i];
            if (s->kind != AST_DECL_VAR && s->kind != AST_DECL_CONST && s->kind != AST_DECL_ARRAY) emit_stmt(&cg, s);
        }
    }
    pop_scope(&cg); emit_ln(&cg, "return 0;"); cg.indent--; emit_ln(&cg, "}");

    FILE* f = fopen(output_c_path, "w");
    if (f) { fputs(cg.out.data, f); fclose(f); }
    str_free(&cg.out); free(cg.structs); free(cg.funcs);
    while(cg.scope_count > 0) {
        pop_scope(&cg); 
    }
    free(cg.scopes);
    return true;
}