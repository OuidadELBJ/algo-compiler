// ir.c  (P-CODE generator, typé pour print: PRNI/PRNR/PRNB/PRNC/PRS)
//
// Convention d'impression (stack-based) :
//   - Pour afficher une valeur => on pousse la valeur sur la pile, puis PRN*.
//   - String literal => LDS "..." puis PRS  (pas de PRS "..." immédiat).
//
// Instructions (principales) :
//   LDA <name>   : push adresse
//   LDI <int>    : push int immediate
//   LDR <real>   : push real immediate (texte)
//   LDS "..."    : push string immediate
//   LDV          : load value from address on stack
//   STO          : store value into address (addr puis val)
//   IDX          : address = base[index]
//   FLD <off>    : address = base + off
//   FLDNAME <f>  : fallback symbolique (si offset inconnu)
//
//   ADD SUB MUL DIV IDIV MOD POW
//   EQ NE LT LE GT GE
//   AND OR NOT NEG
//   JMP Lx, JZ Lx, JNZ Lx
//   CALL name argc
//   RET / RETV
//   POP DUP
//   BRK / QUITFOR
//   HLT
//
// Directives (pour aider la traduction vers C) :
//   .program, .globals/.endglobals, .proc/.endproc, .func/.endfunc, .main/.endmain
//   VAR/CONST/ARRAY/PARAM/LOCAL/LOCAL_ARRAY/LOCAL_CONST + types imprimés

#include "ir.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "token.h"
#include "ast.h"

// =====================
// String builder
// =====================

typedef struct {
    char* data;
    int   len;
    int   cap;
} Str;

static void str_init(Str* s) { s->data = NULL; s->len = 0; s->cap = 0; }

static void str_reserve(Str* s, int need) {
    if (need <= s->cap) return;
    int ncap = (s->cap == 0) ? 1024 : s->cap;
    while (ncap < need) ncap *= 2;
    char* n = (char*)realloc(s->data, (size_t)ncap);
    if (!n) return;
    s->data = n;
    s->cap  = ncap;
}

static void str_append(Str* s, const char* txt) {
    if (!txt) return;
    int n = (int)strlen(txt);
    str_reserve(s, s->len + n + 1);
    memcpy(s->data + s->len, txt, (size_t)n);
    s->len += n;
    s->data[s->len] = '\0';
}

static void str_printf(Str* s, const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str_append(s, buf);
}

static void str_free(Str* s) {
    free(s->data);
    s->data = NULL;
    s->len  = 0;
    s->cap  = 0;
}

// =====================
// Helpers
// =====================

static char* xstrdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* r = (char*)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static char* xstrdup_printf(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return xstrdup(buf);
}

static void str_append_escaped(Str* out, const char* s) {
    str_append(out, "\"");
    if (s) {
        for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
            unsigned char c = *p;
            if (c == '\\') str_append(out, "\\\\");
            else if (c == '\"') str_append(out, "\\\"");
            else if (c == '\n') str_append(out, "\\n");
            else if (c == '\r') str_append(out, "\\r");
            else if (c == '\t') str_append(out, "\\t");
            else {
                char b[2] = {(char)c, 0};
                str_append(out, b);
            }
        }
    }
    str_append(out, "\"");
}

// =====================
// IRProgram object
// =====================

struct IRProgram {
    Str out;
};

static void emit_line(IRProgram* pr, const char* fmt, ...) {
    if (!pr) return;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str_append(&pr->out, buf);
    str_append(&pr->out, "\n");
}

// =====================
// Type system (simple TypeKind for printing)
// =====================

typedef enum {
    TY_UNKNOWN = 0,
    TY_INT,
    TY_REAL,
    TY_BOOL,
    TY_CHAR,
    TY_STRING,
    TY_STRUCT,
    TY_ARRAY
} TypeKind;

typedef struct {
    TypeKind kind;
    TypeKind leaf_kind;       // arrays
    int      dims;            // remaining dims
    const char* struct_name;  // if struct
} ExprType;

static ExprType et_unknown(void) {
    ExprType t; memset(&t, 0, sizeof(t));
    t.kind = TY_UNKNOWN;
    return t;
}
static ExprType et_scalar(TypeKind k) {
    ExprType t; memset(&t, 0, sizeof(t));
    t.kind = k;
    return t;
}
static ExprType et_struct(const char* name) {
    ExprType t; memset(&t, 0, sizeof(t));
    t.kind = TY_STRUCT;
    t.struct_name = name;
    return t;
}
static ExprType et_array(TypeKind leaf, int dims, const char* sname_if_struct) {
    ExprType t; memset(&t, 0, sizeof(t));
    t.kind = TY_ARRAY;
    t.leaf_kind = leaf;
    t.dims = dims;
    t.struct_name = sname_if_struct;
    return t;
}

static TypeKind prim_to_typekind(PrimitiveType pt) {
    switch (pt) {
        case TYPE_ENTIER:    return TY_INT;
        case TYPE_REEL:      return TY_REAL;
        case TYPE_BOOLEEN:   return TY_BOOL;
        case TYPE_CARACTERE: return TY_CHAR;
        case TYPE_CHAINE:    return TY_STRING;
        default:             return TY_UNKNOWN;
    }
}

// analyse un AST type node en ExprType (array compris)
static ExprType type_from_type_node(ASTNode* tn) {
    if (!tn) return et_unknown();

    if (tn->kind == AST_TYPE_PRIMITIVE) {
        return et_scalar(prim_to_typekind(tn->as.type_prim.prim));
    }
    if (tn->kind == AST_TYPE_NAMED) {
        return et_struct(tn->as.type_named.name);
    }
    if (tn->kind == AST_TYPE_ARRAY) {
        int d = tn->as.type_array.dims.count;
        ASTNode* elem = tn->as.type_array.elem_type;
        ExprType base = type_from_type_node(elem);
        if (base.kind == TY_STRUCT) return et_array(TY_STRUCT, d, base.struct_name);
        if (base.kind == TY_ARRAY)  return et_array(base.leaf_kind, d + base.dims, base.struct_name);
        return et_array(base.kind, d, NULL);
    }

    return et_unknown();
}

// Pretty print type (pour directives VAR/PARAM/etc)

static void ptype(Str* out, ASTNode* type_node);

static void ptype_prim(Str* out, PrimitiveType pt) {
    switch (pt) {
        case TYPE_ENTIER:    str_append(out, "int"); break;
        case TYPE_REEL:      str_append(out, "real"); break;
        case TYPE_CARACTERE: str_append(out, "char"); break;
        case TYPE_CHAINE:    str_append(out, "string"); break;
        case TYPE_BOOLEEN:   str_append(out, "bool"); break;
        default:             str_append(out, "?"); break;
    }
}

static void ptype(Str* out, ASTNode* type_node) {
    if (!type_node) { str_append(out, "?"); return; }

    switch (type_node->kind) {
        case AST_TYPE_PRIMITIVE:
            ptype_prim(out, type_node->as.type_prim.prim);
            return;
        case AST_TYPE_NAMED:
            str_append(out, type_node->as.type_named.name ? type_node->as.type_named.name : "?");
            return;
        case AST_TYPE_ARRAY: {
            ptype(out, type_node->as.type_array.elem_type);
            int dims = type_node->as.type_array.dims.count;
            for (int i = 0; i < dims; i++) str_append(out, "[]");
            return;
        }
        default:
            str_append(out, "?");
            return;
    }
}

// =====================
// Symbol environment
// =====================

typedef enum {
    SYM_VAR,
    SYM_CONST,
    SYM_ARRAY,
    SYM_PARAM,
    SYM_FUNC,
    SYM_PROC
} SymKind;

typedef struct {
    char* src_name;
    char* ir_name;
    SymKind kind;

    ASTNode* type_node;   // type réel (elem_type pour arrays)
    bool     is_array;
    int      array_dims;
    const char* struct_name_if_struct;
} Sym;

typedef struct ScopeEnv {
    struct ScopeEnv* parent;
    Sym* items;
    int count;
    int cap;
    int scope_id;
} ScopeEnv;

static ScopeEnv* env_push(ScopeEnv* cur, int scope_id) {
    ScopeEnv* e = (ScopeEnv*)calloc(1, sizeof(ScopeEnv));
    if (!e) return cur;
    e->parent = cur;
    e->scope_id = scope_id;
    return e;
}

static ScopeEnv* env_pop(ScopeEnv* cur) {
    if (!cur) return NULL;
    ScopeEnv* p = cur->parent;
    for (int i = 0; i < cur->count; i++) {
        free(cur->items[i].src_name);
        free(cur->items[i].ir_name);
    }
    free(cur->items);
    free(cur);
    return p;
}

static Sym* env_lookup(ScopeEnv* e, const char* src) {
    for (ScopeEnv* it = e; it; it = it->parent) {
        for (int i = 0; i < it->count; i++) {
            if (it->items[i].src_name && strcmp(it->items[i].src_name, src) == 0) {
                return &it->items[i];
            }
        }
    }
    return NULL;
}

static Sym* env_add(ScopeEnv* e, const char* src, const char* unique,
                    SymKind kind, ASTNode* type_node, bool is_array, int array_dims) {
    if (!e || !src || !unique) return NULL;
    if (e->count >= e->cap) {
        int ncap = (e->cap == 0) ? 16 : e->cap * 2;
        Sym* n = (Sym*)realloc(e->items, (size_t)ncap * sizeof(Sym));
        if (!n) return NULL;
        e->items = n;
        e->cap = ncap;
    }
    Sym* s = &e->items[e->count++];
    memset(s, 0, sizeof(*s));
    s->src_name = xstrdup(src);
    s->ir_name  = xstrdup(unique);
    s->kind = kind;
    s->type_node = type_node;
    s->is_array = is_array;
    s->array_dims = array_dims;

    if (type_node && type_node->kind == AST_TYPE_NAMED) {
        s->struct_name_if_struct = type_node->as.type_named.name;
    }
    if (is_array && type_node && type_node->kind == AST_TYPE_NAMED) {
        s->struct_name_if_struct = type_node->as.type_named.name;
    }

    return s;
}

// =====================
// Struct table: field lookup
// =====================

typedef struct {
    char* field;
    int offset;
    ASTNode* type_node;
} FieldInfo;

typedef struct {
    char* name;
    FieldInfo* fields;
    int count;
    int cap;
} StructInfo;

typedef struct {
    StructInfo* items;
    int count;
    int cap;
} StructTable;

static void stbl_init(StructTable* t) { memset(t, 0, sizeof(*t)); }

static void stbl_free(StructTable* t) {
    if (!t) return;
    for (int i = 0; i < t->count; i++) {
        StructInfo* si = &t->items[i];
        free(si->name);
        for (int j = 0; j < si->count; j++) free(si->fields[j].field);
        free(si->fields);
    }
    free(t->items);
    memset(t, 0, sizeof(*t));
}

static StructInfo* stbl_find(StructTable* t, const char* name) {
    if (!t || !name) return NULL;
    for (int i = 0; i < t->count; i++) {
        if (t->items[i].name && strcmp(t->items[i].name, name) == 0) return &t->items[i];
    }
    return NULL;
}

static StructInfo* stbl_add(StructTable* t, const char* name) {
    if (!t || !name) return NULL;
    if (t->count >= t->cap) {
        int ncap = (t->cap == 0) ? 8 : t->cap * 2;
        StructInfo* n = (StructInfo*)realloc(t->items, (size_t)ncap * sizeof(StructInfo));
        if (!n) return NULL;
        t->items = n;
        t->cap = ncap;
    }
    StructInfo* si = &t->items[t->count++];
    memset(si, 0, sizeof(*si));
    si->name = xstrdup(name);
    return si;
}

static void sinfo_add_field(StructInfo* si, const char* field, int offset, ASTNode* type_node) {
    if (!si || !field) return;
    if (si->count >= si->cap) {
        int ncap = (si->cap == 0) ? 8 : si->cap * 2;
        FieldInfo* n = (FieldInfo*)realloc(si->fields, (size_t)ncap * sizeof(FieldInfo));
        if (!n) return;
        si->fields = n;
        si->cap = ncap;
    }
    FieldInfo* f = &si->fields[si->count++];
    memset(f, 0, sizeof(*f));
    f->field = xstrdup(field);
    f->offset = offset;
    f->type_node = type_node;
}

static int sinfo_field_offset(StructInfo* si, const char* field) {
    if (!si || !field) return -1;
    for (int i = 0; i < si->count; i++) {
        if (si->fields[i].field && strcmp(si->fields[i].field, field) == 0)
            return si->fields[i].offset;
    }
    return -1;
}

static ASTNode* sinfo_field_type(StructInfo* si, const char* field) {
    if (!si || !field) return NULL;
    for (int i = 0; i < si->count; i++) {
        if (si->fields[i].field && strcmp(si->fields[i].field, field) == 0)
            return si->fields[i].type_node;
    }
    return NULL;
}

static void stbl_build_from_program(StructTable* st, ASTNode* program) {
    if (!st || !program || program->kind != AST_PROGRAM) return;
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d || d->kind != AST_DEF_STRUCT) continue;

        StructInfo* si = stbl_add(st, d->as.def_struct.name);
        if (!si) continue;

        int off = 0;
        for (int f = 0; f < d->as.def_struct.fields.count; f++) {
            ASTNode* fld = d->as.def_struct.fields.items[f];
            if (!fld || fld->kind != AST_FIELD) continue;
            sinfo_add_field(si, fld->as.field.name, off, fld->as.field.type);
            off++;
        }
    }
}

// =====================
// Function signature table
// =====================

typedef struct {
    char* name;
    bool is_func;
    ASTNode* return_type; // NULL for proc
} DefSig;

typedef struct {
    DefSig* items;
    int count;
    int cap;
} DefSigTable;

static void dsig_init(DefSigTable* t) { memset(t, 0, sizeof(*t)); }

static void dsig_free(DefSigTable* t) {
    for (int i = 0; i < t->count; i++) free(t->items[i].name);
    free(t->items);
    memset(t, 0, sizeof(*t));
}

static void dsig_add(DefSigTable* t, const char* name, bool is_func, ASTNode* ret_type) {
    if (!t || !name) return;
    if (t->count >= t->cap) {
        int ncap = (t->cap == 0) ? 16 : t->cap * 2;
        DefSig* n = (DefSig*)realloc(t->items, (size_t)ncap * sizeof(DefSig));
        if (!n) return;
        t->items = n;
        t->cap = ncap;
    }
    t->items[t->count].name = xstrdup(name);
    t->items[t->count].is_func = is_func;
    t->items[t->count].return_type = ret_type;
    t->count++;
}

static DefSig* dsig_find(DefSigTable* t, const char* name) {
    if (!t || !name) return NULL;
    for (int i = 0; i < t->count; i++) {
        if (t->items[i].name && strcmp(t->items[i].name, name) == 0) return &t->items[i];
    }
    return NULL;
}

static bool dsig_is_func(DefSigTable* t, const char* name) {
    DefSig* s = dsig_find(t, name);
    return s ? s->is_func : false;
}

static ASTNode* dsig_ret_type(DefSigTable* t, const char* name) {
    DefSig* s = dsig_find(t, name);
    return s ? s->return_type : NULL;
}

static void dsig_build_from_program(DefSigTable* t, ASTNode* program) {
    if (!t || !program || program->kind != AST_PROGRAM) return;
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;
        if (d->kind == AST_DEF_FUNC)
            dsig_add(t, d->as.def_func.name, true, d->as.def_func.return_type);
        else if (d->kind == AST_DEF_PROC)
            dsig_add(t, d->as.def_proc.name, false, NULL);
    }
}

// =====================
// Codegen context
// =====================

typedef struct BreakCtx {
    int break_label;
    struct BreakCtx* prev;
} BreakCtx;

typedef struct {
    IRProgram* pr;
    StructTable structs;
    DefSigTable sigs;
    ScopeEnv* env;
    int next_scope_id;
    int next_label;
    BreakCtx* break_stack;
} CG;

static int new_label(CG* cg) { return ++cg->next_label; }

static void cg_push_scope(CG* cg) { cg->env = env_push(cg->env, ++cg->next_scope_id); }
static void cg_pop_scope(CG* cg)  { cg->env = env_pop(cg->env); }

static void break_push(CG* cg, int label) {
    BreakCtx* c = (BreakCtx*)malloc(sizeof(BreakCtx));
    if (!c) return;
    c->break_label = label;
    c->prev = cg->break_stack;
    cg->break_stack = c;
}

static void break_pop(CG* cg) {
    if (!cg || !cg->break_stack) return;
    BreakCtx* c = cg->break_stack;
    cg->break_stack = c->prev;
    free(c);
}

static int break_top(CG* cg) {
    return (cg && cg->break_stack) ? cg->break_stack->break_label : -1;
}

static const char* cg_name(CG* cg, const char* src) {
    Sym* s = env_lookup(cg->env, src);
    return (s && s->ir_name) ? s->ir_name : src;
}

// Forward declarations
static void cg_stmt(CG* cg, ASTNode* st);
static void cg_block(CG* cg, ASTNode* block);
static void cg_expr(CG* cg, ASTNode* e);
static ExprType cg_typeof(CG* cg, ASTNode* e);

// ---------- Address generation for lvalues ----------
static void cg_addr(CG* cg, ASTNode* lvalue) {
    if (!cg || !lvalue) return;

    switch (lvalue->kind) {
        case AST_IDENT: {
            const char* n = cg_name(cg, lvalue->as.ident.name);
            emit_line(cg->pr, "LDA %s", n);
            return;
        }
        case AST_INDEX: {
            cg_addr(cg, lvalue->as.index.base);
            cg_expr(cg, lvalue->as.index.index);
            emit_line(cg->pr, "IDX");
            return;
        }
        case AST_FIELD_ACCESS: {
            ASTNode* base = lvalue->as.field_access.base;
            const char* field = lvalue->as.field_access.field;

            cg_addr(cg, base);

            const char* struct_name = NULL;

            if (base && base->kind == AST_IDENT) {
                Sym* sb = env_lookup(cg->env, base->as.ident.name);
                if (sb) {
                    if (!sb->is_array && sb->type_node && sb->type_node->kind == AST_TYPE_NAMED)
                        struct_name = sb->type_node->as.type_named.name;
                    else if (sb->is_array && sb->struct_name_if_struct)
                        struct_name = sb->struct_name_if_struct;
                }
            } else {
                ExprType bt = cg_typeof(cg, base);
                if (bt.kind == TY_STRUCT) struct_name = bt.struct_name;
            }

            int off = -1;
            if (struct_name) {
                StructInfo* si = stbl_find(&cg->structs, struct_name);
                if (si) off = sinfo_field_offset(si, field);
            }

            if (off >= 0) emit_line(cg->pr, "FLD %d", off);
            else          emit_line(cg->pr, "FLDNAME %s", field);

            return;
        }
        default:
            emit_line(cg->pr, "# ERROR: invalid lvalue kind=%d", (int)lvalue->kind);
            return;
    }
}

// ---------- Expression codegen (push value on stack) ----------
static void cg_expr(CG* cg, ASTNode* e) {
    if (!cg || !e) { emit_line(cg->pr, "LDI 0"); return; }

    switch (e->kind) {
        case AST_LITERAL_INT:
            emit_line(cg->pr, "LDI %lld", e->as.lit_int.value);
            return;
        case AST_LITERAL_REAL:
            emit_line(cg->pr, "LDR %s", e->as.lit_real.text ? e->as.lit_real.text : "0.0");
            return;
        case AST_LITERAL_BOOL:
            emit_line(cg->pr, "LDI %d", e->as.lit_bool.value ? 1 : 0);
            return;
        case AST_LITERAL_STRING: {
            Str tmp;
            str_init(&tmp);
            str_append(&tmp, "LDS ");
            str_append_escaped(&tmp, e->as.lit_string.text ? e->as.lit_string.text : "");
            emit_line(cg->pr, "%s", tmp.data ? tmp.data : "LDS \"\"");
            str_free(&tmp);
            return;
        }
       case AST_IDENT: {
    const char* src = e->as.ident.name;
    const char* n   = cg_name(cg, src);

    // On regarde le symbole pour savoir si c'est un tableau
    Sym* s = env_lookup(cg->env, src);
    bool is_array = (s && s->is_array);

    if (is_array) {
        // On veut la base du tableau (pour l’indexation ou passage en param)
        emit_line(cg->pr, "LDA %s", n);
    } else {
        // Cas normal : variable scalaire
        emit_line(cg->pr, "LDA %s", n);
        emit_line(cg->pr, "LDV");
    }
    return;
}
        case AST_INDEX:
        case AST_FIELD_ACCESS:
            cg_addr(cg, e);
            emit_line(cg->pr, "LDV");
            return;

        case AST_UNARY: {
            cg_expr(cg, e->as.unary.expr);
            if (e->as.unary.op == TOK_NON) emit_line(cg->pr, "NOT");
            else if (e->as.unary.op == TOK_MOINS) emit_line(cg->pr, "NEG");
            else emit_line(cg->pr, "# ERROR: unary op %d", (int)e->as.unary.op);
            return;
        }

        case AST_BINARY: {
            cg_expr(cg, e->as.binary.lhs);
            cg_expr(cg, e->as.binary.rhs);

            TokenType op = e->as.binary.op;
            switch (op) {
                case TOK_PLUS:           emit_line(cg->pr, "ADD"); break;
                case TOK_MOINS:          emit_line(cg->pr, "SUB"); break;
                case TOK_FOIS:           emit_line(cg->pr, "MUL"); break;
                case TOK_DIVISE:         emit_line(cg->pr, "DIV"); break;
                case TOK_DIV_ENTIER:     emit_line(cg->pr, "IDIV"); break;
                case TOK_MODULO:         emit_line(cg->pr, "MOD"); break;
                case TOK_PUISSANCE:      emit_line(cg->pr, "POW"); break;

                case TOK_ET:             emit_line(cg->pr, "AND"); break;
                case TOK_OU:             emit_line(cg->pr, "OR");  break;

                case TOK_EGAL:           emit_line(cg->pr, "EQ"); break;
                case TOK_DIFFERENT:      emit_line(cg->pr, "NE"); break;
                case TOK_INFERIEUR:      emit_line(cg->pr, "LT"); break;
                case TOK_INFERIEUR_EGAL: emit_line(cg->pr, "LE"); break;
                case TOK_SUPERIEUR:      emit_line(cg->pr, "GT"); break;
                case TOK_SUPERIEUR_EGAL: emit_line(cg->pr, "GE"); break;

                default:
                    emit_line(cg->pr, "# ERROR: binary op %d", (int)op);
                    break;
            }
            return;
        }

        case AST_CALL: {
            ASTNode* callee = e->as.call.callee;
            const char* name = NULL;
            if (callee && callee->kind == AST_IDENT) name = callee->as.ident.name;

            int argc = e->as.call.args.count;
            for (int i = 0; i < argc; i++) cg_expr(cg, e->as.call.args.items[i]);

            emit_line(cg->pr, "CALL %s %d", name ? name : "<?>", argc);
            return;
        }

        default:
            emit_line(cg->pr, "# ERROR: expr kind=%d", (int)e->kind);
            emit_line(cg->pr, "LDI 0");
            return;
    }
}

// ---------- Type inference for printing ----------
static ExprType cg_typeof_ident(CG* cg, const char* name) {
    Sym* s = env_lookup(cg->env, name);
    if (!s) return et_unknown();

    if (s->is_array) {
        ExprType leaf = type_from_type_node(s->type_node);
        if (leaf.kind == TY_STRUCT) return et_array(TY_STRUCT, s->array_dims, leaf.struct_name);
        if (leaf.kind == TY_ARRAY)  return et_array(leaf.leaf_kind, s->array_dims + leaf.dims, leaf.struct_name);
        return et_array(leaf.kind, s->array_dims, NULL);
    }

    ExprType t = type_from_type_node(s->type_node);
    return t;
}

static ExprType cg_typeof(CG* cg, ASTNode* e) {
    if (!cg || !e) return et_unknown();

    switch (e->kind) {
        case AST_LITERAL_INT:    return et_scalar(TY_INT);
        case AST_LITERAL_REAL:   return et_scalar(TY_REAL);
        case AST_LITERAL_BOOL:   return et_scalar(TY_BOOL);
        case AST_LITERAL_STRING: return et_scalar(TY_STRING);

        case AST_IDENT:
            return cg_typeof_ident(cg, e->as.ident.name);

        case AST_INDEX: {
            ExprType bt = cg_typeof(cg, e->as.index.base);
            if (bt.kind == TY_ARRAY && bt.dims > 0) {
                int nd = bt.dims - 1;
                if (nd == 0) {
                    if (bt.leaf_kind == TY_STRUCT) return et_struct(bt.struct_name);
                    return et_scalar(bt.leaf_kind);
                }
                return et_array(bt.leaf_kind, nd, bt.struct_name);
            }
            return et_unknown();
        }

        case AST_FIELD_ACCESS: {
            ExprType bt = cg_typeof(cg, e->as.field_access.base);
            if (bt.kind != TY_STRUCT) return et_unknown();

            const char* sname = bt.struct_name;
            if (!sname) return et_unknown();

            StructInfo* si = stbl_find(&cg->structs, sname);
            if (!si) return et_unknown();

            ASTNode* ftype = sinfo_field_type(si, e->as.field_access.field);
            return type_from_type_node(ftype);
        }

        case AST_CALL: {
            ASTNode* callee = e->as.call.callee;
            const char* name = NULL;
            if (callee && callee->kind == AST_IDENT) name = callee->as.ident.name;
            ASTNode* rt = dsig_ret_type(&cg->sigs, name ? name : "");
            return type_from_type_node(rt);
        }

        case AST_UNARY: {
            ExprType t = cg_typeof(cg, e->as.unary.expr);
            if (e->as.unary.op == TOK_NON)  return et_scalar(TY_BOOL);
            if (e->as.unary.op == TOK_MOINS) {
                if (t.kind == TY_REAL) return et_scalar(TY_REAL);
                if (t.kind == TY_INT)  return et_scalar(TY_INT);
                return t;
            }
            return t;
        }

        case AST_BINARY: {
            TokenType op = e->as.binary.op;

            switch (op) {
                case TOK_ET:
                case TOK_OU:
                case TOK_EGAL:
                case TOK_DIFFERENT:
                case TOK_INFERIEUR:
                case TOK_INFERIEUR_EGAL:
                case TOK_SUPERIEUR:
                case TOK_SUPERIEUR_EGAL:
                    return et_scalar(TY_BOOL);
                default:
                    break;
            }

            ExprType a = cg_typeof(cg, e->as.binary.lhs);
            ExprType b = cg_typeof(cg, e->as.binary.rhs);
            if (a.kind == TY_REAL || b.kind == TY_REAL) return et_scalar(TY_REAL);
            if (a.kind == TY_INT  && b.kind == TY_INT)  return et_scalar(TY_INT);
            if (op == TOK_DIVISE) return et_scalar(TY_REAL);

            return et_unknown();
        }

        default:
            return et_unknown();
    }
}

// ---------- Statements ----------

static void cg_stmt_assign(CG* cg, ASTNode* st) {
    cg_addr(cg, st->as.assign.target);
    cg_expr(cg, st->as.assign.value);
    emit_line(cg->pr, "STO");
}

static void cg_emit_print(CG* cg, ExprType t) {
    switch (t.kind) {
        case TY_INT:    emit_line(cg->pr, "PRNI"); break;
        case TY_REAL:   emit_line(cg->pr, "PRNR"); break;
        case TY_BOOL:   emit_line(cg->pr, "PRNB"); break;
        case TY_CHAR:   emit_line(cg->pr, "PRNC"); break;
        case TY_STRING: emit_line(cg->pr, "PRS");  break;
        default:        emit_line(cg->pr, "PRNI"); break;
    }
}

static void cg_stmt_write(CG* cg, ASTNode* st) {
    int n = st->as.write_stmt.args.count;
    for (int i = 0; i < n; i++) {
        ASTNode* a = st->as.write_stmt.args.items[i];
        cg_expr(cg, a);
        ExprType t = cg_typeof(cg, a);
        cg_emit_print(cg, t);
    }
}

static void cg_stmt_read(CG* cg, ASTNode* st) {
    int n = st->as.read_stmt.targets.count;
    for (int i = 0; i < n; i++) {
        cg_addr(cg, st->as.read_stmt.targets.items[i]);
        emit_line(cg->pr, "IN");
    }
}

static void cg_stmt_return(CG* cg, ASTNode* st) {
    if (st->as.ret_stmt.value) {
        cg_expr(cg, st->as.ret_stmt.value);
        emit_line(cg->pr, "RETV");
    } else {
        emit_line(cg->pr, "RET");
    }
}

static void cg_stmt_call(CG* cg, ASTNode* st) {
    ASTNode* call = st->as.call_stmt.call;
    if (!call || call->kind != AST_CALL) return;

    cg_expr(cg, call);

    ASTNode* callee = call->as.call.callee;
    if (callee && callee->kind == AST_IDENT) {
        if (dsig_is_func(&cg->sigs, callee->as.ident.name)) {
            emit_line(cg->pr, "POP");
        }
    }
}

static void cg_block(CG* cg, ASTNode* block);

static void cg_stmt_if(CG* cg, ASTNode* st) {
    int endL  = new_label(cg);
    int elseL = new_label(cg);

    cg_expr(cg, st->as.if_stmt.cond);
    emit_line(cg->pr, "JZ L%d", elseL);

    cg_block(cg, st->as.if_stmt.then_block);
    emit_line(cg->pr, "JMP L%d", endL);

    emit_line(cg->pr, "L%d:", elseL);

    int n = st->as.if_stmt.elif_conds.count;
    for (int i = 0; i < n; i++) {
        int nextElse = new_label(cg);
        cg_expr(cg, st->as.if_stmt.elif_conds.items[i]);
        emit_line(cg->pr, "JZ L%d", nextElse);
        cg_block(cg, st->as.if_stmt.elif_blocks.items[i]);
        emit_line(cg->pr, "JMP L%d", endL);
        emit_line(cg->pr, "L%d:", nextElse);
    }

    if (st->as.if_stmt.else_block) cg_block(cg, st->as.if_stmt.else_block);

    emit_line(cg->pr, "L%d:", endL);
}

static void cg_stmt_while(CG* cg, ASTNode* st) {
    int startL = new_label(cg);
    int endL   = new_label(cg);

    break_push(cg, endL);

    emit_line(cg->pr, "L%d:", startL);
    cg_expr(cg, st->as.while_stmt.cond);
    emit_line(cg->pr, "JZ L%d", endL);
    cg_block(cg, st->as.while_stmt.body);
    emit_line(cg->pr, "JMP L%d", startL);
    emit_line(cg->pr, "L%d:", endL);

    break_pop(cg);
}

static void cg_stmt_for(CG* cg, ASTNode* st) {
    const char* vsrc = st->as.for_stmt.var;
    const char* v    = cg_name(cg, vsrc);

    ASTNode* step = st->as.for_stmt.step;
    bool step_is_neg_const = false;
    if (step && step->kind == AST_LITERAL_INT && step->as.lit_int.value < 0) {
        step_is_neg_const = true;
    }

    // init : v = start
    emit_line(cg->pr, "LDA %s", v);
    cg_expr(cg, st->as.for_stmt.start);
    emit_line(cg->pr, "STO");

    int startL = new_label(cg);
    int endL   = new_label(cg);

    break_push(cg, endL);

    emit_line(cg->pr, "L%d:", startL);

    // condition
    if (step_is_neg_const) {
        // boucle décroissante : tant que v >= end
        emit_line(cg->pr, "LDA %s", v);
        emit_line(cg->pr, "LDV");
        cg_expr(cg, st->as.for_stmt.end);
        emit_line(cg->pr, "LT");           // v < end ?
        emit_line(cg->pr, "JNZ L%d", endL);
    } else {
        // boucle croissante (ou pas dynamique) : tant que v <= end
        emit_line(cg->pr, "LDA %s", v);
        emit_line(cg->pr, "LDV");
        cg_expr(cg, st->as.for_stmt.end);
        emit_line(cg->pr, "GT");           // v > end ?
        emit_line(cg->pr, "JNZ L%d", endL);
    }

    // corps
    cg_block(cg, st->as.for_stmt.body);

    // inc
    emit_line(cg->pr, "LDA %s", v);
    emit_line(cg->pr, "LDA %s", v);
    emit_line(cg->pr, "LDV");
    if (step) cg_expr(cg, step);
    else emit_line(cg->pr, "LDI 1");
    emit_line(cg->pr, "ADD");
    emit_line(cg->pr, "STO");

    emit_line(cg->pr, "JMP L%d", startL);
    emit_line(cg->pr, "L%d:", endL);

    break_pop(cg);
}

static void cg_stmt_repeat(CG* cg, ASTNode* st) {
    int startL = new_label(cg);
    int endL   = new_label(cg);

    break_push(cg, endL);

    emit_line(cg->pr, "L%d:", startL);
    cg_block(cg, st->as.repeat_stmt.body);
    if (st->as.repeat_stmt.until_cond) {
        cg_expr(cg, st->as.repeat_stmt.until_cond);
        // repeat ... until cond -> on répète tant que cond est FAUSSE
        emit_line(cg->pr, "JNZ L%d", startL);
    }
    emit_line(cg->pr, "L%d:", endL);

    break_pop(cg);
}

static void cg_stmt_break(CG* cg) {
    int lbl = break_top(cg);
    if (lbl < 0) {
        emit_line(cg->pr, "# ERROR: break hors boucle/switch");
        return;
    }
    emit_line(cg->pr, "JMP L%d", lbl);
}

static void cg_stmt_quitfor(CG* cg) {
    int lbl = break_top(cg);
    if (lbl < 0) {
        emit_line(cg->pr, "# ERROR: quitter_pour hors boucle");
        return;
    }
    emit_line(cg->pr, "JMP L%d", lbl);
}

static void cg_stmt_switch(CG* cg, ASTNode* st) {
    if (!cg || !st) return;

    ASTNode* expr = st->as.switch_stmt.expr;
    ASTList* cases = &st->as.switch_stmt.cases;
    ASTNode* def_block = st->as.switch_stmt.default_block;

    int n = cases->count;
    int endL = new_label(cg);

    break_push(cg, endL);

    int* testL = (int*)malloc(sizeof(int) * (n + 1));
    int* bodyL = (int*)malloc(sizeof(int) * n);
    if (!testL || !bodyL) {
        emit_line(cg->pr, "# ERROR: switch malloc failed");
        free(testL);
        free(bodyL);
        break_pop(cg);
        return;
    }

    for (int i = 0; i < n; i++) {
        testL[i] = new_label(cg);
        bodyL[i] = new_label(cg);
    }
    int defaultL = def_block ? new_label(cg) : endL;
    testL[n] = defaultL;

    // saut initial vers la zone de tests
    if (n > 0 || def_block) {
        emit_line(cg->pr, "JMP L%d", testL[0]);
    }

    // corps des cases
    for (int i = 0; i < n; i++) {
        ASTNode* c = cases->items[i];
        if (!c || c->kind != AST_CASE) continue;

        emit_line(cg->pr, "L%d:", bodyL[i]);
        if (c->as.case_stmt.body) {
            cg_block(cg, c->as.case_stmt.body);
        }
        emit_line(cg->pr, "JMP L%d", endL);
    }

    // default
    if (def_block) {
        emit_line(cg->pr, "L%d:", defaultL);
        cg_block(cg, def_block);
        emit_line(cg->pr, "JMP L%d", endL);
    }

    // zone de tests
    for (int i = 0; i < n; i++) {
        ASTNode* c = cases->items[i];
        if (!c || c->kind != AST_CASE) continue;

        emit_line(cg->pr, "L%d:", testL[i]);

        ASTList* vals = &c->as.case_stmt.values;
        int nbv = vals->count;

        if (nbv == 0) {
            emit_line(cg->pr, "JMP L%d", bodyL[i]);
            continue;
        }

        for (int j = 0; j < nbv; j++) {
            ASTNode* val = vals->items[j];

            // Version simple : on réévalue expr à chaque test.
            cg_expr(cg, expr);
            cg_expr(cg, val);
            emit_line(cg->pr, "EQ");

            if (j + 1 < nbv) {
                int nextValL = new_label(cg);
                emit_line(cg->pr, "JZ L%d", nextValL);
                emit_line(cg->pr, "JMP L%d", bodyL[i]);
                emit_line(cg->pr, "L%d:", nextValL);
            } else {
                emit_line(cg->pr, "JZ L%d", testL[i + 1]);
                emit_line(cg->pr, "JMP L%d", bodyL[i]);
            }
        }
    }

    emit_line(cg->pr, "L%d:", endL);

    free(testL);
    free(bodyL);

    break_pop(cg);
}

static void cg_stmt(CG* cg, ASTNode* st) {
    if (!cg || !st) return;

    switch (st->kind) {
        case AST_DECL_VAR:
        case AST_DECL_CONST:
        case AST_DECL_ARRAY:
            return;

        case AST_ASSIGN:     cg_stmt_assign(cg, st); return;
        case AST_WRITE:      cg_stmt_write(cg, st);  return;
        case AST_READ:       cg_stmt_read(cg, st);   return;
        case AST_RETURN:     cg_stmt_return(cg, st); return;
        case AST_CALL_STMT:  cg_stmt_call(cg, st);   return;

        case AST_IF:         cg_stmt_if(cg, st);     return;
        case AST_WHILE:      cg_stmt_while(cg, st);  return;
        case AST_FOR:        cg_stmt_for(cg, st);    return;
        case AST_REPEAT:     cg_stmt_repeat(cg, st); return;
        case AST_BREAK:      cg_stmt_break(cg);      return;
        case AST_QUIT_FOR:   cg_stmt_quitfor(cg);    return;
        case AST_SWITCH:     cg_stmt_switch(cg, st); return;

        default:
            emit_line(cg->pr, "# WARNING: stmt kind=%d not generated", (int)st->kind);
            return;
    }
}

// ---------- Declarations ----------

static void cg_emit_decl_line(CG* cg, const char* kind, const char* name,
                              ASTNode* type_node, bool is_array, int array_dims,
                              const char* extra) {
    Str t;
    str_init(&t);

    if (is_array) {
        if (type_node && type_node->kind != AST_TYPE_ARRAY) {
            ptype(&t, type_node);
            for (int i = 0; i < array_dims; i++) str_append(&t, "[]");
        } else {
            ptype(&t, type_node);
        }
    } else {
        ptype(&t, type_node);
    }

    if (extra) emit_line(cg->pr, "%s %s : %s %s", kind, name, t.data ? t.data : "?", extra);
    else       emit_line(cg->pr, "%s %s : %s",       kind, name, t.data ? t.data : "?");

    str_free(&t);
}

static void cg_decl(CG* cg, ASTNode* d, bool is_global, const char* func_prefix) {
    if (!cg || !d) return;

    const char* src = NULL;
    SymKind kind = SYM_VAR;
    ASTNode* type_node = NULL;
    bool is_array = false;
    int array_dims = 0;

    if (d->kind == AST_DECL_VAR) {
        src = d->as.decl_var.name;
        kind = SYM_VAR;
        type_node = d->as.decl_var.type;

        if (type_node && type_node->kind == AST_TYPE_ARRAY) {
            is_array = true;
            array_dims = type_node->as.type_array.dims.count;
            type_node = type_node->as.type_array.elem_type;
        }

    } else if (d->kind == AST_DECL_CONST) {
        src = d->as.decl_const.name;
        kind = SYM_CONST;
        type_node = d->as.decl_const.type;

    } else if (d->kind == AST_DECL_ARRAY) {
        src = d->as.decl_array.name;
        kind = SYM_ARRAY;
        type_node = d->as.decl_array.elem_type;
        is_array = true;
        array_dims = d->as.decl_array.dims.count;

    } else {
        return;
    }

    char* unique = NULL;
    if (is_global) unique = xstrdup(src);
    else {
        if (func_prefix) unique = xstrdup_printf("%s$S%d_%s", func_prefix, cg->env ? cg->env->scope_id : 0, src);
        else unique = xstrdup_printf("S%d_%s", cg->env ? cg->env->scope_id : 0, src);
    }

    env_add(cg->env, src, unique, kind, type_node, is_array, array_dims);

    if (d->kind == AST_DECL_CONST) {
        Str extraS;
        str_init(&extraS);
        const char* extra = NULL;

        ASTNode* v = d->as.decl_const.value;
        if (v) {
            if (v->kind == AST_LITERAL_INT)  { str_printf(&extraS, "= %lld", v->as.lit_int.value); extra = extraS.data; }
            else if (v->kind == AST_LITERAL_REAL) { str_printf(&extraS, "= %s", v->as.lit_real.text ? v->as.lit_real.text : "0.0"); extra = extraS.data; }
            else if (v->kind == AST_LITERAL_BOOL) { str_printf(&extraS, "= %d", v->as.lit_bool.value ? 1 : 0); extra = extraS.data; }
            else if (v->kind == AST_LITERAL_STRING) {
                str_append(&extraS, "= ");
                str_append_escaped(&extraS, v->as.lit_string.text ? v->as.lit_string.text : "");
                extra = extraS.data;
            }
        }

        cg_emit_decl_line(cg, is_global ? "CONST" : "LOCAL_CONST", unique, type_node, false, 0, extra);
        str_free(&extraS);
    }
    else if (is_array) {
        cg_emit_decl_line(cg, is_global ? "ARRAY" : "LOCAL_ARRAY", unique, type_node, true, array_dims, NULL);
    }
    else {
        cg_emit_decl_line(cg, is_global ? "VAR" : "LOCAL", unique, type_node, false, 0, NULL);
    }

    free(unique);
}

// ---------- Blocks ----------

static void cg_block(CG* cg, ASTNode* block) {
    if (!cg || !block || block->kind != AST_BLOCK) return;

    cg_push_scope(cg);

    for (int i = 0; i < block->as.block.stmts.count; i++) {
        ASTNode* n = block->as.block.stmts.items[i];
        if (!n) continue;
        if (n->kind == AST_DECL_VAR || n->kind == AST_DECL_CONST || n->kind == AST_DECL_ARRAY) {
            cg_decl(cg, n, false, NULL);
        }
    }

    for (int i = 0; i < block->as.block.stmts.count; i++) {
        ASTNode* n = block->as.block.stmts.items[i];
        if (!n) continue;
        if (n->kind == AST_DECL_VAR || n->kind == AST_DECL_CONST || n->kind == AST_DECL_ARRAY) continue;
        cg_stmt(cg, n);
    }

    cg_pop_scope(cg);
}

// ---------- Definitions ----------

static void cg_def_func(CG* cg, ASTNode* def) {
    const char* name = def->as.def_func.name;
    emit_line(cg->pr, "");
    emit_line(cg->pr, ".func %s", name ? name : "<?>");

    cg_push_scope(cg);

    for (int i = 0; i < def->as.def_func.params.count; i++) {
        ASTNode* p = def->as.def_func.params.items[i];
        if (!p || p->kind != AST_PARAM) continue;

        const char* pn = p->as.param.name;

        bool is_array = false;
        int dims = 0;
        ASTNode* tn = p->as.param.type;

        if (tn && tn->kind == AST_TYPE_ARRAY) {
            is_array = true;
            dims = tn->as.type_array.dims.count;
            tn = tn->as.type_array.elem_type;
        }

        env_add(cg->env, pn, pn, SYM_PARAM, tn, is_array, dims);
        cg_emit_decl_line(cg, "PARAM", pn, tn, is_array, dims, NULL);
    }

    {
        Str rt; str_init(&rt);
        ptype(&rt, def->as.def_func.return_type);
        emit_line(cg->pr, "RET_TYPE %s", rt.data ? rt.data : "?");
        str_free(&rt);
    }

    emit_line(cg->pr, ".code");

    if (def->as.def_func.body) cg_block(cg, def->as.def_func.body);

    emit_line(cg->pr, "LDI 0");
    emit_line(cg->pr, "RETV");
    emit_line(cg->pr, ".endfunc %s", name ? name : "<?>");

    cg_pop_scope(cg);
}

static void cg_def_proc(CG* cg, ASTNode* def) {
    const char* name = def->as.def_proc.name;
    emit_line(cg->pr, "");
    emit_line(cg->pr, ".proc %s", name ? name : "<?>");

    cg_push_scope(cg);

    for (int i = 0; i < def->as.def_proc.params.count; i++) {
        ASTNode* p = def->as.def_proc.params.items[i];
        if (!p || p->kind != AST_PARAM) continue;

        const char* pn = p->as.param.name;

        bool is_array = false;
        int dims = 0;
        ASTNode* tn = p->as.param.type;

        if (tn && tn->kind == AST_TYPE_ARRAY) {
            is_array = true;
            dims = tn->as.type_array.dims.count;
            tn = tn->as.type_array.elem_type;
        }

        env_add(cg->env, pn, pn, SYM_PARAM, tn, is_array, dims);
        cg_emit_decl_line(cg, "PARAM", pn, tn, is_array, dims, NULL);
    }

    emit_line(cg->pr, "RET_TYPE void");
    emit_line(cg->pr, ".code");

    if (def->as.def_proc.body) cg_block(cg, def->as.def_proc.body);

    emit_line(cg->pr, "RET");
    emit_line(cg->pr, ".endproc %s", name ? name : "<?>");

    cg_pop_scope(cg);
}

// =====================
// Public API
// =====================

IRProgram* ir_generate(ASTNode* program) {
    if (!program || program->kind != AST_PROGRAM) return NULL;

    IRProgram* pr = (IRProgram*)calloc(1, sizeof(IRProgram));
    if (!pr) return NULL;
    str_init(&pr->out);

    CG cg;
    memset(&cg, 0, sizeof(cg));
    cg.pr = pr;
    stbl_init(&cg.structs);
    dsig_init(&cg.sigs);
    stbl_build_from_program(&cg.structs, program);
    dsig_build_from_program(&cg.sigs, program);

    // global scope
    cg.env = env_push(NULL, 0);
    cg.next_scope_id = 0;
    cg.next_label = 0;

    emit_line(pr, ".program %s", program->as.program.name ? program->as.program.name : "<?>");

    // globals
    emit_line(pr, ".globals");
    for (int i = 0; i < program->as.program.decls.count; i++) {
        ASTNode* d = program->as.program.decls.items[i];
        if (!d) continue;
        cg_decl(&cg, d, true, NULL);
    }
    emit_line(pr, ".endglobals");

    // defs
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;

        if (d->kind == AST_DEF_FUNC)      cg_def_func(&cg, d);
        else if (d->kind == AST_DEF_PROC) cg_def_proc(&cg, d);
        else if (d->kind == AST_DEF_STRUCT) {
            emit_line(pr, "");
            emit_line(pr, "# struct %s", d->as.def_struct.name ? d->as.def_struct.name : "<?>");
            StructInfo* si = stbl_find(&cg.structs, d->as.def_struct.name);
            if (si) {
                for (int f = 0; f < si->count; f++) {
                    Str t; str_init(&t);
                    ptype(&t, si->fields[f].type_node);
                    emit_line(pr, "#   field %s @%d : %s",
                              si->fields[f].field, si->fields[f].offset,
                              t.data ? t.data : "?");
                    str_free(&t);
                }
            }
        }
    }

    // main
    emit_line(pr, "");
    emit_line(pr, ".main");
    emit_line(pr, ".code");
    if (program->as.program.main_block) cg_block(&cg, program->as.program.main_block);
    emit_line(pr, "HLT");
    emit_line(pr, ".endmain");

    // cleanup
    while (cg.env) cg.env = env_pop(cg.env);
    while (cg.break_stack) break_pop(&cg);
    stbl_free(&cg.structs);
    dsig_free(&cg.sigs);

    return pr;
}

void ir_print(IRProgram* prog, FILE* out) {
    if (!prog) return;
    if (!out) out = stdout;
    fputs(prog->out.data ? prog->out.data : "", out);
}

void ir_free(IRProgram* prog) {
    if (!prog) return;
    str_free(&prog->out);
    free(prog);
}
