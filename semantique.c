#include "semantique.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


static char* sdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* r = (char*)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static void sem_error(SemContext* ctx, ASTNode* node, const char* fmt, ...) {
    if (!ctx) return;

    if (ctx->err_count >= ctx->err_cap) {
        int ncap = (ctx->err_cap == 0) ? 16 : ctx->err_cap * 2;
        char** n = (char**)realloc(ctx->errors, (size_t)ncap * sizeof(char*));
        if (!n) return;
        ctx->errors = n;
        ctx->err_cap = ncap;
    }

    char buf[1024];
    int line = node ? node->line : 0;
    int col  = node ? node->col  : 0;

    int off = snprintf(buf, sizeof(buf), "%d:%d: ", line, col);
    if (off < 0) off = 0;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + off, sizeof(buf) - (size_t)off, fmt, ap);
    va_end(ap);

    ctx->errors[ctx->err_count++] = sdup(buf);
}

void sem_print_errors(SemContext* ctx) {
    if (!ctx || ctx->err_count == 0) {
        printf("Aucune erreur sémantique.\n");
        return;
    }
    printf("=== Erreurs sémantiques (%d) ===\n", ctx->err_count);
    for (int i = 0; i < ctx->err_count; i++) {
        printf(" %s\n", ctx->errors[i]);
    }
}

// Construction / comparaison des types

static Type* type_new(TypeKind k) {
    Type* t = (Type*)calloc(1, sizeof(Type));
    if (!t) return NULL;
    t->kind = k;
    return t;
}

static Type* type_make_prim(TypeKind k) { return type_new(k); }
static Type* type_make_void(void) { return type_make_prim(TY_VOID); }
static Type* type_make_error(void){ return type_make_prim(TY_ERROR); }

static Type* type_make_array(Type* elem, int dims) {
    Type* t = type_new(TY_ARRAY);
    if (!t) return NULL;
    t->as.array.elem = elem;
    t->as.array.dims = dims;
    return t;
}

static Type* type_make_struct(const char* name) {
    Type* t = type_new(TY_STRUCT);
    if (!t) return NULL;
    t->as.st.name = sdup(name);
    return t;
}

static bool type_is_numeric(Type* t) {
    if (!t) return false;
    return t->kind == TY_INT || t->kind == TY_REAL || t->kind == TY_CHAR;
}

static bool type_is_integral(Type* t) {
    if (!t) return false;
    return t->kind == TY_INT || t->kind == TY_CHAR || t->kind == TY_BOOL;
}

static bool type_equal(Type* a, Type* b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    if (a->kind == TY_ARRAY) {
        return a->as.array.dims == b->as.array.dims && type_equal(a->as.array.elem, b->as.array.elem);
    }
    if (a->kind == TY_STRUCT) {
        if (!a->as.st.name || !b->as.st.name) return false;
        return strcmp(a->as.st.name, b->as.st.name) == 0;
    }
    return true;
}

static bool type_assignable(Type* dst, Type* src) {
    if (!dst || !src) return false;
    if (dst->kind == TY_ERROR || src->kind == TY_ERROR) return true;

    if (type_equal(dst, src)) return true;

    // Conversion implicite int -> real
    if (dst->kind == TY_REAL && (src->kind == TY_INT || src->kind == TY_CHAR || src->kind == TY_BOOL)) return true;

    // Conversion char -> int
    if (dst->kind == TY_INT && src->kind == TY_CHAR) return true;

    // Conversion bool -> int (optionnel)
    if (dst->kind == TY_INT && src->kind == TY_BOOL) return true;

    return false;
}

static Type* type_decay_index(Type* base) {
    if (!base || base->kind != TY_ARRAY) return type_make_error();
    int d = base->as.array.dims;
    if (d <= 1) return base->as.array.elem;
    return type_make_array(base->as.array.elem, d - 1);
}

// Gestion des scopes / symboles

static Scope* scope_push(SemContext* ctx) {
    Scope* s = (Scope*)calloc(1, sizeof(Scope));
    if (!s) return NULL;
    s->parent = ctx->scope;
    ctx->scope = s;
    return s;
}

static void symbol_free(Symbol* sym) {
    if (!sym) return;
    free(sym->name);

    if (sym->param_types) free(sym->param_types);
    if (sym->param_names) {
        for (int i = 0; i < sym->param_count; i++) free(sym->param_names[i]);
        free(sym->param_names);
    }
}

static void scope_pop(SemContext* ctx) {
    if (!ctx || !ctx->scope) return;
    Scope* s = ctx->scope;
    ctx->scope = s->parent;

    for (int i = 0; i < s->count; i++) symbol_free(&s->symbols[i]);
    free(s->symbols);
    free(s);
}

static Symbol* scope_lookup_here(Scope* s, const char* name) {
    if (!s || !name) return NULL;
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->symbols[i].name, name) == 0) return &s->symbols[i];
    }
    return NULL;
}

static Symbol* scope_lookup(Scope* s, const char* name) {
    for (Scope* cur = s; cur; cur = cur->parent) {
        Symbol* sym = scope_lookup_here(cur, name);
        if (sym) return sym;
    }
    return NULL;
}

static Symbol* scope_add(Scope* s, const char* name) {
    if (!s || !name) return NULL;
    if (s->count >= s->cap) {
        int ncap = (s->cap == 0) ? 16 : s->cap * 2;
        Symbol* n = (Symbol*)realloc(s->symbols, (size_t)ncap * sizeof(Symbol));
        if (!n) return NULL;
        s->symbols = n;
        s->cap = ncap;
    }
    Symbol* sym = &s->symbols[s->count++];
    memset(sym, 0, sizeof(*sym));
    sym->name = sdup(name);
    return sym;
}

// Déclarations anticipées (pour éviter les "implicit declaration" en C)

static void sem_declare_var(SemContext* ctx, ASTNode* decl);
static void sem_declare_const(SemContext* ctx, ASTNode* decl);
static void sem_declare_array(SemContext* ctx, ASTNode* decl);

// Conversion AST(Type) -> Type*

static Type* sem_type_from_ast(SemContext* ctx, ASTNode* type_node) {
    (void)ctx;
    if (!type_node) return type_make_error();

    if (type_node->kind == AST_TYPE_PRIMITIVE) {
        switch (type_node->as.type_prim.prim) {
            case TYPE_ENTIER:    return type_make_prim(TY_INT);
            case TYPE_REEL:      return type_make_prim(TY_REAL);
            case TYPE_CARACTERE: return type_make_prim(TY_CHAR);
            case TYPE_CHAINE:    return type_make_prim(TY_STRING);
            case TYPE_BOOLEEN:   return type_make_prim(TY_BOOL);
            default:             return type_make_error();
        }
    }

    if (type_node->kind == AST_TYPE_NAMED) {
        return type_make_struct(type_node->as.type_named.name);
    }

    if (type_node->kind == AST_TYPE_ARRAY) {
        Type* elem = sem_type_from_ast(ctx, type_node->as.type_array.elem_type);
        int dims = type_node->as.type_array.dims.count;
        return type_make_array(elem, dims);
    }

    return type_make_error();
}

// Évaluation de constantes entières (dimensions de tableaux / labels de cas)

static bool sem_const_int_value(SemContext* ctx, ASTNode* expr, long long* out);

static bool sem_const_int_ident(SemContext* ctx, ASTNode* expr, long long* out) {
    if (!expr || expr->kind != AST_IDENT) return false;
    Symbol* sym = scope_lookup(ctx->scope, expr->as.ident.name);
    if (!sym) return false;
    if (sym->kind != SYM_CONST) return false;
    if (!sym->has_int_value) return false;
    *out = sym->int_value;
    return true;
}

static bool sem_const_int_value(SemContext* ctx, ASTNode* expr, long long* out) {
    if (!expr) return false;

    if (expr->kind == AST_LITERAL_INT) {
        *out = expr->as.lit_int.value;
        return true;
    }

    if (expr->kind == AST_IDENT) {
        return sem_const_int_ident(ctx, expr, out);
    }

    if (expr->kind == AST_UNARY) {
        long long v;
        if (!sem_const_int_value(ctx, expr->as.unary.expr, &v)) return false;
        if (expr->as.unary.op == TOK_MOINS) { *out = -v; return true; }
        return false;
    }

    if (expr->kind == AST_BINARY) {
        long long a, b;
        if (!sem_const_int_value(ctx, expr->as.binary.lhs, &a)) return false;
        if (!sem_const_int_value(ctx, expr->as.binary.rhs, &b)) return false;

        switch (expr->as.binary.op) {
            case TOK_PLUS: *out = a + b; return true;
            case TOK_MOINS: *out = a - b; return true;
            case TOK_FOIS: *out = a * b; return true;
            case TOK_DIVISE:
            case TOK_DIV_ENTIER:
                if (b == 0) return false;
                *out = a / b; return true;
            case TOK_MODULO:
                if (b == 0) return false;
                *out = a % b; return true;
            default:
                return false;
        }
    }

    return false;
}

// Typage des expressions

static bool is_lvalue(ASTNode* e) {
    if (!e) return false;
    return e->kind == AST_IDENT || e->kind == AST_INDEX || e->kind == AST_FIELD_ACCESS;
}

static Type* sem_expr(SemContext* ctx, ASTNode* expr);

static Type* sem_ident(SemContext* ctx, ASTNode* expr) {
    Symbol* sym = scope_lookup(ctx->scope, expr->as.ident.name);
    if (!sym) {
        sem_error(ctx, expr, "Identifiant non déclaré: '%s'", expr->as.ident.name);
        return type_make_error();
    }

    switch (sym->kind) {
        case SYM_VAR:
        case SYM_CONST:
        case SYM_ARRAY:
        case SYM_PARAM:
            return sym->type ? sym->type : type_make_error();
        case SYM_FUNC:
        case SYM_PROC:
            sem_error(ctx, expr, "Le nom '%s' est une procédure/fonction et ne peut pas être utilisé comme valeur ici.", sym->name);
            return type_make_error();
        case SYM_STRUCT:
            sem_error(ctx, expr, "'%s' est un type structure, pas une valeur.", sym->name);
            return type_make_error();
        default:
            return type_make_error();
    }
}

static Type* sem_literal(SemContext* ctx, ASTNode* expr) {
    (void)ctx;
    switch (expr->kind) {
        case AST_LITERAL_INT:    return type_make_prim(TY_INT);
        case AST_LITERAL_REAL:   return type_make_prim(TY_REAL);
        case AST_LITERAL_STRING: return type_make_prim(TY_STRING);
        case AST_LITERAL_BOOL:   return type_make_prim(TY_BOOL);
        default: return type_make_error();
    }
}

static Type* sem_unary(SemContext* ctx, ASTNode* expr) {
    Type* t = sem_expr(ctx, expr->as.unary.expr);

    if (expr->as.unary.op == TOK_NON) {
        if (t->kind != TY_BOOL && t->kind != TY_ERROR) {
            sem_error(ctx, expr, "Opérateur 'Non' attend un booléen.");
            return type_make_error();
        }
        return type_make_prim(TY_BOOL);
    }

    if (expr->as.unary.op == TOK_MOINS) {
        if (!type_is_numeric(t) && t->kind != TY_ERROR) {
            sem_error(ctx, expr, "Le '-' unaire attend un type numérique.");
            return type_make_error();
        }
        return t;
    }

    sem_error(ctx, expr, "Opérateur unaire inconnu.");
    return type_make_error();
}

static Type* sem_binary(SemContext* ctx, ASTNode* expr) {
    Type* lt = sem_expr(ctx, expr->as.binary.lhs);
    Type* rt = sem_expr(ctx, expr->as.binary.rhs);

    TokenType op = expr->as.binary.op;

    if (op == TOK_ET || op == TOK_OU) {
        if (lt->kind != TY_BOOL && lt->kind != TY_ERROR) sem_error(ctx, expr, "'Et/Ou' attend des booléens.");
        if (rt->kind != TY_BOOL && rt->kind != TY_ERROR) sem_error(ctx, expr, "'Et/Ou' attend des booléens.");
        return type_make_prim(TY_BOOL);
    }

    if (op == TOK_PLUS || op == TOK_MOINS || op == TOK_FOIS || op == TOK_DIVISE || op == TOK_DIV_ENTIER || op == TOK_MODULO || op == TOK_PUISSANCE) {
        if (!type_is_numeric(lt) && lt->kind != TY_ERROR) sem_error(ctx, expr, "Opération arithmétique: gauche non numérique.");
        if (!type_is_numeric(rt) && rt->kind != TY_ERROR) sem_error(ctx, expr, "Opération arithmétique: droite non numérique.");

        if (lt->kind == TY_REAL || rt->kind == TY_REAL) return type_make_prim(TY_REAL);
        return type_make_prim(TY_INT);
    }

    if (op == TOK_INFERIEUR || op == TOK_INFERIEUR_EGAL || op == TOK_SUPERIEUR || op == TOK_SUPERIEUR_EGAL) {
        if (!type_is_numeric(lt) && lt->kind != TY_ERROR) sem_error(ctx, expr, "Comparaison: gauche non numérique.");
        if (!type_is_numeric(rt) && rt->kind != TY_ERROR) sem_error(ctx, expr, "Comparaison: droite non numérique.");
        return type_make_prim(TY_BOOL);
    }

    if (op == TOK_EGAL || op == TOK_DIFFERENT) {
        if (type_is_numeric(lt) && type_is_numeric(rt)) return type_make_prim(TY_BOOL);

        if (!type_equal(lt, rt) && lt->kind != TY_ERROR && rt->kind != TY_ERROR) {
            sem_error(ctx, expr, "Comparaison '='/'<>' entre types incompatibles.");
        }
        return type_make_prim(TY_BOOL);
    }

    sem_error(ctx, expr, "Opérateur binaire non géré.");
    return type_make_error();
}

static Type* sem_index(SemContext* ctx, ASTNode* expr) {
    Type* bt = sem_expr(ctx, expr->as.index.base);
    Type* it = sem_expr(ctx, expr->as.index.index);

    if (it->kind != TY_INT && it->kind != TY_CHAR && it->kind != TY_BOOL && it->kind != TY_ERROR) {
        sem_error(ctx, expr, "Index de tableau doit être entier (ou compatible).");
    }

    if (bt->kind != TY_ARRAY && bt->kind != TY_ERROR) {
        sem_error(ctx, expr, "Indexation '[]' sur une valeur qui n'est pas un tableau.");
        return type_make_error();
    }

    return type_decay_index(bt);
}

static Symbol* lookup_struct_symbol(SemContext* ctx, const char* name) {
    Symbol* s = scope_lookup(ctx->scope, name);
    if (!s) return NULL;
    if (s->kind != SYM_STRUCT) return NULL;
    return s;
}

static Type* sem_field_access(SemContext* ctx, ASTNode* expr) {
    Type* bt = sem_expr(ctx, expr->as.field_access.base);

    if (bt->kind != TY_STRUCT && bt->kind != TY_ERROR) {
        sem_error(ctx, expr, "Accès champ '.' sur une valeur non-structure.");
        return type_make_error();
    }
    if (bt->kind == TY_ERROR) return type_make_error();

    Symbol* st = lookup_struct_symbol(ctx, bt->as.st.name);
    if (!st) {
        sem_error(ctx, expr, "Type structure inconnu: '%s'", bt->as.st.name ? bt->as.st.name : "?");
        return type_make_error();
    }

    const char* fname = expr->as.field_access.field;

    for (int i = 0; i < st->param_count; i++) {
        if (st->param_names && strcmp(st->param_names[i], fname) == 0) {
            return st->param_types[i];
        }
    }

    sem_error(ctx, expr, "Champ '%s' inexistant dans la structure '%s'.", fname, bt->as.st.name);
    return type_make_error();
}

static Type* sem_call(SemContext* ctx, ASTNode* expr) {
    ASTNode* callee = expr->as.call.callee;
    if (!callee || callee->kind != AST_IDENT) {
        sem_error(ctx, expr, "Appel: le callee doit être un identifiant (ex: f(...)).");
        return type_make_error();
    }

    Symbol* sym = scope_lookup(ctx->scope, callee->as.ident.name);
    if (!sym) {
        sem_error(ctx, expr, "Fonction/Procédure non déclarée: '%s'", callee->as.ident.name);
        return type_make_error();
    }

    if (sym->kind != SYM_FUNC && sym->kind != SYM_PROC) {
        sem_error(ctx, expr, "'%s' n'est pas une fonction/procédure.", sym->name);
        return type_make_error();
    }

    int ac = expr->as.call.args.count;
    int pc = sym->param_count;

    if (ac != pc) {
        sem_error(ctx, expr, "Appel '%s': mauvais nombre d'arguments (%d au lieu de %d).", sym->name, ac, pc);
    }

    int n = (ac < pc) ? ac : pc;
    for (int i = 0; i < n; i++) {
        Type* at = sem_expr(ctx, expr->as.call.args.items[i]);
        Type* pt = sym->param_types[i];
        if (!type_assignable(pt, at)) {
            sem_error(ctx, expr, "Appel '%s': argument %d incompatible.", sym->name, i + 1);
        }
    }

    if (sym->kind == SYM_PROC) return type_make_void();
    return sym->return_type ? sym->return_type : type_make_error();
}

static Type* sem_expr(SemContext* ctx, ASTNode* expr) {
    if (!expr) return type_make_error();

    switch (expr->kind) {
        case AST_IDENT:          return sem_ident(ctx, expr);

        case AST_LITERAL_INT:
        case AST_LITERAL_REAL:
        case AST_LITERAL_STRING:
        case AST_LITERAL_BOOL:   return sem_literal(ctx, expr);

        case AST_UNARY:          return sem_unary(ctx, expr);
        case AST_BINARY:         return sem_binary(ctx, expr);

        case AST_INDEX:          return sem_index(ctx, expr);
        case AST_FIELD_ACCESS:   return sem_field_access(ctx, expr);
        case AST_CALL:           return sem_call(ctx, expr);

        default:
            sem_error(ctx, expr, "Expression non gérée (kind=%d).", (int)expr->kind);
            return type_make_error();
    }
}

// Instructions + Blocs

static void sem_stmt(SemContext* ctx, ASTNode* st);

static void sem_handle_decl(SemContext* ctx, ASTNode* d) {
    switch (d->kind) {
        case AST_DECL_VAR:   sem_declare_var(ctx, d);   break;
        case AST_DECL_CONST: sem_declare_const(ctx, d); break;
        case AST_DECL_ARRAY: sem_declare_array(ctx, d); break;
        default:
            sem_error(ctx, d, "Déclaration locale inconnue (kind=%d).", (int)d->kind);
            break;
    }
}

// Chaque bloc = nouvelle portée + support des déclarations locales
static void sem_block(SemContext* ctx, ASTNode* block) {
    if (!block || block->kind != AST_BLOCK) return;

    scope_push(ctx);

    for (int i = 0; i < block->as.block.stmts.count; i++) {
        ASTNode* n = block->as.block.stmts.items[i];
        if (!n) continue;

        if (n->kind == AST_DECL_VAR || n->kind == AST_DECL_CONST || n->kind == AST_DECL_ARRAY) {
            sem_handle_decl(ctx, n);
            continue;
        }

        sem_stmt(ctx, n);
    }

    scope_pop(ctx);
}

static void sem_assign(SemContext* ctx, ASTNode* st) {
    ASTNode* target = st->as.assign.target;
    ASTNode* value  = st->as.assign.value;

    if (!is_lvalue(target)) {
        sem_error(ctx, st, "Affectation: la cible n'est pas assignable (lvalue).");
    }

    // Interdire l'écriture dans un identifiant constant
    if (target && target->kind == AST_IDENT) {
        Symbol* sym = scope_lookup(ctx->scope, target->as.ident.name);
        if (sym && sym->kind == SYM_CONST) {
            sem_error(ctx, st, "Affectation: impossible de modifier la constante '%s'.", sym->name);
        }
    }

    Type* tt = sem_expr(ctx, target);
    Type* vt = sem_expr(ctx, value);

    if (!type_assignable(tt, vt)) {
        sem_error(ctx, st, "Affectation: types incompatibles.");
    }
}

static void sem_if(SemContext* ctx, ASTNode* st) {
    Type* ct = sem_expr(ctx, st->as.if_stmt.cond);
    if (ct->kind != TY_BOOL && ct->kind != TY_ERROR) {
        sem_error(ctx, st, "Condition de Si doit être booléenne.");
    }

    sem_block(ctx, st->as.if_stmt.then_block);

    // elif_conds et elif_blocks sont deux listes parallèles
    int n = st->as.if_stmt.elif_conds.count;
    for (int i = 0; i < n; i++) {
        Type* ect = sem_expr(ctx, st->as.if_stmt.elif_conds.items[i]);
        if (ect->kind != TY_BOOL && ect->kind != TY_ERROR) {
            sem_error(ctx, st, "Condition de SinonSi doit être booléenne.");
        }
        sem_block(ctx, st->as.if_stmt.elif_blocks.items[i]);
    }

    if (st->as.if_stmt.else_block) sem_block(ctx, st->as.if_stmt.else_block);
}

static void sem_while(SemContext* ctx, ASTNode* st) {
    Type* ct = sem_expr(ctx, st->as.while_stmt.cond);
    if (ct->kind != TY_BOOL && ct->kind != TY_ERROR) {
        sem_error(ctx, st, "Condition de TantQue doit être booléenne.");
    }

    ctx->loop_depth++;
    sem_block(ctx, st->as.while_stmt.body);
    ctx->loop_depth--;
}

static void sem_for(SemContext* ctx, ASTNode* st) {
    Symbol* v = scope_lookup(ctx->scope, st->as.for_stmt.var);
    if (!v) {
        sem_error(ctx, st, "Pour: variable de boucle '%s' non déclarée.", st->as.for_stmt.var);
    } else {
        if (v->kind == SYM_CONST) sem_error(ctx, st, "Pour: variable de boucle '%s' ne peut pas être une constante.", v->name);
        if (v->type && v->type->kind != TY_INT && v->type->kind != TY_CHAR && v->type->kind != TY_BOOL && v->type->kind != TY_ERROR) {
            sem_error(ctx, st, "Pour: variable de boucle '%s' doit être entière.", v->name);
        }
    }

    Type* s = sem_expr(ctx, st->as.for_stmt.start);
    Type* e = sem_expr(ctx, st->as.for_stmt.end);
    if (!type_is_integral(s) && s->kind != TY_ERROR) sem_error(ctx, st, "Pour: start doit être entier.");
    if (!type_is_integral(e) && e->kind != TY_ERROR) sem_error(ctx, st, "Pour: end doit être entier.");

    if (st->as.for_stmt.step) {
        Type* p = sem_expr(ctx, st->as.for_stmt.step);
        if (!type_is_integral(p) && p->kind != TY_ERROR) sem_error(ctx, st, "Pour: pas/step doit être entier.");
    }

    ctx->loop_depth++;
    ctx->for_depth++;
    sem_block(ctx, st->as.for_stmt.body);
    ctx->for_depth--;
    ctx->loop_depth--;
}

static void sem_repeat(SemContext* ctx, ASTNode* st) {
    ctx->loop_depth++;
    sem_block(ctx, st->as.repeat_stmt.body);
    ctx->loop_depth--;

    if (st->as.repeat_stmt.until_cond) {
        Type* ct = sem_expr(ctx, st->as.repeat_stmt.until_cond);
        if (ct->kind != TY_BOOL && ct->kind != TY_ERROR) {
            sem_error(ctx, st, "Repeter: condition doit être booléenne.");
        }
    }
}

static void sem_return(SemContext* ctx, ASTNode* st) {
    if (ctx->in_procedure) {
        if (st->as.ret_stmt.value != NULL) {
            sem_error(ctx, st, "Procédure: 'Retourner' ne doit pas retourner de valeur.");
        }
        return;
    }

    if (!ctx->in_function) {
        sem_error(ctx, st, "'Retourner' hors d'une fonction/procédure.");
        return;
    }

    Type* expected = ctx->current_return_type ? ctx->current_return_type : type_make_error();
    Type* got = st->as.ret_stmt.value ? sem_expr(ctx, st->as.ret_stmt.value) : type_make_void();

    if (!type_assignable(expected, got)) {
        sem_error(ctx, st, "Retourner: type retourné incompatible.");
    }
}

static void sem_write(SemContext* ctx, ASTNode* st) {
    for (int i = 0; i < st->as.write_stmt.args.count; i++) {
        (void)sem_expr(ctx, st->as.write_stmt.args.items[i]);
    }
}

static void sem_read(SemContext* ctx, ASTNode* st) {
    for (int i = 0; i < st->as.read_stmt.targets.count; i++) {
        ASTNode* t = st->as.read_stmt.targets.items[i];
        if (!is_lvalue(t)) sem_error(ctx, st, "Lire: cible non assignable.");
        if (t && t->kind == AST_IDENT) {
            Symbol* sym = scope_lookup(ctx->scope, t->as.ident.name);
            if (sym && sym->kind == SYM_CONST) sem_error(ctx, st, "Lire: impossible de lire dans la constante '%s'.", sym->name);
        }
        (void)sem_expr(ctx, t);
    }
}

static void sem_call_stmt(SemContext* ctx, ASTNode* st) {
    // C'est un AST_CALL_STMT qui contient une expression AST_CALL
    if (!st->as.call_stmt.call || st->as.call_stmt.call->kind != AST_CALL) {
        sem_error(ctx, st, "Appel (stmt): noeud invalide.");
        return;
    }
    (void)sem_expr(ctx, st->as.call_stmt.call);
}

static void sem_break(SemContext* ctx, ASTNode* st) {
    (void)st;
    if (ctx->loop_depth == 0 && ctx->switch_depth == 0) {
        sem_error(ctx, st, "'Sortir' est autorisé seulement dans une boucle ou un Selon.");
    }
}

static void sem_quit_for(SemContext* ctx, ASTNode* st) {
    (void)st;
    if (ctx->for_depth == 0) {
        sem_error(ctx, st, "'Quitter Pour' est autorisé seulement à l'intérieur d'un Pour.");
    }
}

static void sem_switch(SemContext* ctx, ASTNode* st) {
    Type* et = sem_expr(ctx, st->as.switch_stmt.expr);

    if (!type_is_integral(et) && et->kind != TY_ERROR) {
        sem_error(ctx, st, "Selon: expression doit être entière/compatible (entier, caractere, booleen).");
    }

    // Vérifier les labels des "Cas" : constante entière + doublons
    long long* seen = NULL;
    int seen_count = 0, seen_cap = 0;

    ctx->switch_depth++;

    for (int i = 0; i < st->as.switch_stmt.cases.count; i++) {
        ASTNode* c = st->as.switch_stmt.cases.items[i];
        if (!c || c->kind != AST_CASE) continue;

        // Valeurs des labels de cas
        for (int j = 0; j < c->as.case_stmt.values.count; j++) {
            ASTNode* lab = c->as.case_stmt.values.items[j];

            // Le label doit être une constante
            long long v;
            if (!sem_const_int_value(ctx, lab, &v)) {
                sem_error(ctx, lab, "Cas: label doit être une constante entière (ou constante entière via ident).");
            } else {
                // Détection des doublons
                bool dup = false;
                for (int k = 0; k < seen_count; k++) {
                    if (seen[k] == v) { dup = true; break; }
                }
                if (dup) sem_error(ctx, lab, "Cas: label dupliqué (%lld).", v);

                if (seen_count >= seen_cap) {
                    int ncap = (seen_cap == 0) ? 16 : seen_cap * 2;
                    long long* nn = (long long*)realloc(seen, (size_t)ncap * sizeof(long long));
                    if (nn) { seen = nn; seen_cap = ncap; }
                }
                if (seen && seen_count < seen_cap) seen[seen_count++] = v;
            }

            // Compatibilité de type (approximative) : label intégral
            Type* lt = sem_expr(ctx, lab);
            if (!type_is_integral(lt) && lt->kind != TY_ERROR) {
                sem_error(ctx, lab, "Cas: label doit être entier/compatible.");
            }
        }

        // Corps du case
        sem_block(ctx, c->as.case_stmt.body);
    }

    if (st->as.switch_stmt.default_block) sem_block(ctx, st->as.switch_stmt.default_block);

    ctx->switch_depth--;

    free(seen);
}

static void sem_stmt(SemContext* ctx, ASTNode* st) {
    if (!st) return;

    switch (st->kind) {
        case AST_ASSIGN:     sem_assign(ctx, st); break;
        case AST_IF:         sem_if(ctx, st); break;
        case AST_WHILE:      sem_while(ctx, st); break;
        case AST_FOR:        sem_for(ctx, st); break;
        case AST_REPEAT:     sem_repeat(ctx, st); break;
        case AST_CALL_STMT:  sem_call_stmt(ctx, st); break;
        case AST_RETURN:     sem_return(ctx, st); break;
        case AST_WRITE:      sem_write(ctx, st); break;
        case AST_READ:       sem_read(ctx, st); break;
        case AST_BREAK:      sem_break(ctx, st); break;
        case AST_QUIT_FOR:   sem_quit_for(ctx, st); break;
        case AST_SWITCH:     sem_switch(ctx, st); break;
        default:
            sem_error(ctx, st, "Instruction non gérée (kind=%d).", (int)st->kind);
            break;
    }
}

// Déclarations / définitions

static void sem_declare_struct(SemContext* ctx, ASTNode* def) {
    const char* name = def->as.def_struct.name;
    if (scope_lookup_here(ctx->scope, name)) {
        sem_error(ctx, def, "Double déclaration du symbole '%s'.", name);
        return;
    }

    Symbol* sym = scope_add(ctx->scope, name);
    sym->kind = SYM_STRUCT;
    sym->type = type_make_struct(name);

    // On réutilise (param_names/param_types/param_count) pour stocker les champs
    int fc = def->as.def_struct.fields.count;
    sym->param_count = fc;
    sym->param_names = (char**)calloc((size_t)fc, sizeof(char*));
    sym->param_types = (Type**)calloc((size_t)fc, sizeof(Type*));

    for (int i = 0; i < fc; i++) {
        ASTNode* f = def->as.def_struct.fields.items[i];
        if (!f || f->kind != AST_FIELD) continue;

        const char* fname = f->as.field.name;
        // Champ dupliqué
        for (int j = 0; j < i; j++) {
            if (sym->param_names[j] && strcmp(sym->param_names[j], fname) == 0) {
                sem_error(ctx, f, "Champ dupliqué '%s' dans structure '%s'.", fname, name);
            }
        }

        sym->param_names[i] = sdup(fname);
        sym->param_types[i] = sem_type_from_ast(ctx, f->as.field.type);
    }
}

static void sem_declare_var(SemContext* ctx, ASTNode* decl) {
    const char* name = decl->as.decl_var.name;
    if (scope_lookup_here(ctx->scope, name)) {
        sem_error(ctx, decl, "Double déclaration de '%s'.", name);
        return;
    }

    Symbol* sym = scope_add(ctx->scope, name);
    sym->kind = SYM_VAR;
    sym->type = sem_type_from_ast(ctx, decl->as.decl_var.type);

    // Si c'est un type struct nommé, il doit exister
    if (decl->as.decl_var.type && decl->as.decl_var.type->kind == AST_TYPE_NAMED) {
        Symbol* st = lookup_struct_symbol(ctx, decl->as.decl_var.type->as.type_named.name);
        if (!st) sem_error(ctx, decl, "Type structure inconnu: '%s'.", decl->as.decl_var.type->as.type_named.name);
    }
}

static void sem_declare_const(SemContext* ctx, ASTNode* decl) {
    const char* name = decl->as.decl_const.name;
    if (scope_lookup_here(ctx->scope, name)) {
        sem_error(ctx, decl, "Double déclaration de '%s'.", name);
        return;
    }

    Symbol* sym = scope_add(ctx->scope, name);
    sym->kind = SYM_CONST;
    sym->type = sem_type_from_ast(ctx, decl->as.decl_const.type);

    Type* vt = sem_expr(ctx, decl->as.decl_const.value);
    if (!type_assignable(sym->type, vt)) {
        sem_error(ctx, decl, "Constante '%s': valeur incompatible avec son type.", name);
    }

    // Pré-calculer la valeur si c'est une constante entière
    long long v;
    if (sym->type && sym->type->kind == TY_INT) {
        if (sem_const_int_value(ctx, decl->as.decl_const.value, &v)) {
            sym->has_int_value = true;
            sym->int_value = v;
        }
    }
}

static void sem_declare_array(SemContext* ctx, ASTNode* decl) {
    const char* name = decl->as.decl_array.name;
    if (scope_lookup_here(ctx->scope, name)) {
        sem_error(ctx, decl, "Double déclaration de '%s'.", name);
        return;
    }

    int dims = decl->as.decl_array.dims.count;
    if (dims <= 0) sem_error(ctx, decl, "Tableau '%s' doit avoir au moins une dimension.", name);

    Type* elem = sem_type_from_ast(ctx, decl->as.decl_array.elem_type);

    // Vérifier que les dimensions sont des constantes entières
    for (int i = 0; i < dims; i++) {
        long long v;
        if (!sem_const_int_value(ctx, decl->as.decl_array.dims.items[i], &v)) {
            sem_error(ctx, decl->as.decl_array.dims.items[i], "Dimension de tableau doit être une constante entière.");
        } else {
            if (v <= 0) sem_error(ctx, decl->as.decl_array.dims.items[i], "Dimension de tableau doit être > 0.");
        }
    }

    Symbol* sym = scope_add(ctx->scope, name);
    sym->kind = SYM_ARRAY;
    sym->type = type_make_array(elem, dims);

    // Si l'élément est un struct nommé, il doit exister
    if (decl->as.decl_array.elem_type && decl->as.decl_array.elem_type->kind == AST_TYPE_NAMED) {
        Symbol* st = lookup_struct_symbol(ctx, decl->as.decl_array.elem_type->as.type_named.name);
        if (!st) sem_error(ctx, decl, "Type structure inconnu: '%s'.", decl->as.decl_array.elem_type->as.type_named.name);
    }
}

static void sem_predeclare_funcproc(SemContext* ctx, ASTNode* def, bool is_proc) {
    const char* name = is_proc ? def->as.def_proc.name : def->as.def_func.name;

    if (scope_lookup_here(ctx->scope, name)) {
        sem_error(ctx, def, "Double déclaration de fonction/procédure '%s'.", name);
        return;
    }

    Symbol* sym = scope_add(ctx->scope, name);
    sym->kind = is_proc ? SYM_PROC : SYM_FUNC;

    // Paramètres
    ASTList* params = is_proc ? &def->as.def_proc.params : &def->as.def_func.params;
    int pc = params->count;

    sym->param_count = pc;
    sym->param_types = (Type**)calloc((size_t)pc, sizeof(Type*));
    sym->param_names = (char**)calloc((size_t)pc, sizeof(char*));

    for (int i = 0; i < pc; i++) {
        ASTNode* p = params->items[i];
        if (!p || p->kind != AST_PARAM) continue;
        sym->param_names[i] = sdup(p->as.param.name);
        sym->param_types[i] = sem_type_from_ast(ctx, p->as.param.type);

        // Paramètre dupliqué
        for (int j = 0; j < i; j++) {
            if (sym->param_names[j] && strcmp(sym->param_names[j], sym->param_names[i]) == 0) {
                sem_error(ctx, p, "Paramètre dupliqué '%s' dans '%s'.", sym->param_names[i], name);
            }
        }
    }

    if (is_proc) {
        sym->return_type = type_make_void();
        sym->type = sym->return_type;
    } else {
        sym->return_type = sem_type_from_ast(ctx, def->as.def_func.return_type);
        sym->type = sym->return_type;
    }
}

static void sem_check_funcproc_body(SemContext* ctx, ASTNode* def, bool is_proc) {
    // Nouvelle portée pour les paramètres
    scope_push(ctx);

    const char* name = is_proc ? def->as.def_proc.name : def->as.def_func.name;

    // Mettre les paramètres dans la portée
    ASTList* params = is_proc ? &def->as.def_proc.params : &def->as.def_func.params;
    for (int i = 0; i < params->count; i++) {
        ASTNode* p = params->items[i];
        if (!p || p->kind != AST_PARAM) continue;

        if (scope_lookup_here(ctx->scope, p->as.param.name)) {
            sem_error(ctx, p, "Paramètre '%s' dupliqué (scope).", p->as.param.name);
            continue;
        }
        Symbol* s = scope_add(ctx->scope, p->as.param.name);
        s->kind = SYM_PARAM;
        s->type = sem_type_from_ast(ctx, p->as.param.type);
    }

    // Configurer le contexte de retour
    bool old_in_func = ctx->in_function;
    bool old_in_proc = ctx->in_procedure;
    Type* old_ret = ctx->current_return_type;

    ctx->in_function = !is_proc;
    ctx->in_procedure = is_proc;
    ctx->current_return_type = is_proc ? type_make_void() : sem_type_from_ast(ctx, def->as.def_func.return_type);

    ASTNode* body = is_proc ? def->as.def_proc.body : def->as.def_func.body;
    if (!body) sem_error(ctx, def, "Corps manquant dans '%s'.", name);
    else sem_block(ctx, body);

    // Restaurer
    ctx->in_function = old_in_func;
    ctx->in_procedure = old_in_proc;
    ctx->current_return_type = old_ret;

    scope_pop(ctx);
}

// API publique

void sem_init(SemContext* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    scope_push(ctx); // Scope globale
}

void sem_free(SemContext* ctx) {
    if (!ctx) return;

    while (ctx->scope) scope_pop(ctx);

    for (int i = 0; i < ctx->err_count; i++) free(ctx->errors[i]);
    free(ctx->errors);
}

bool sem_analyze_program(SemContext* ctx, ASTNode* program) {
    if (!ctx || !program || program->kind != AST_PROGRAM) return false;

    // 1) Déclarer les structures d'abord (pour que les types des variables puissent les référencer)
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (d && d->kind == AST_DEF_STRUCT) {
            sem_declare_struct(ctx, d);
        }
    }

    // 2) Déclarer les globales (var/const/array)
    for (int i = 0; i < program->as.program.decls.count; i++) {
        ASTNode* d = program->as.program.decls.items[i];
        if (!d) continue;

        switch (d->kind) {
            case AST_DECL_VAR:   sem_declare_var(ctx, d); break;
            case AST_DECL_CONST: sem_declare_const(ctx, d); break;
            case AST_DECL_ARRAY: sem_declare_array(ctx, d); break;
            default:
                sem_error(ctx, d, "Déclaration globale inconnue (kind=%d).", (int)d->kind);
                break;
        }
    }

    // 3) Pré-déclarer les fonctions/procédures
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;
        if (d->kind == AST_DEF_FUNC) sem_predeclare_funcproc(ctx, d, false);
        else if (d->kind == AST_DEF_PROC) sem_predeclare_funcproc(ctx, d, true);
    }

    // 4) Vérifier les corps des fonctions/procédures
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;
        if (d->kind == AST_DEF_FUNC) sem_check_funcproc_body(ctx, d, false);
        else if (d->kind == AST_DEF_PROC) sem_check_funcproc_body(ctx, d, true);
    }

    // 5) Bloc principal
    if (!program->as.program.main_block) {
        sem_error(ctx, program, "Main block manquant.");
    } else {
        // Le main n'est ni dans une fonction ni dans une procédure
        bool old_in_func = ctx->in_function;
        bool old_in_proc = ctx->in_procedure;
        Type* old_ret = ctx->current_return_type;

        ctx->in_function = false;
        ctx->in_procedure = false;
        ctx->current_return_type = NULL;

        sem_block(ctx, program->as.program.main_block);

        ctx->in_function = old_in_func;
        ctx->in_procedure = old_in_proc;
        ctx->current_return_type = old_ret;
    }

    return ctx->err_count == 0;
}
