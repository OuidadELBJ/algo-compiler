// parser.c
#include "parser.h"
#include "ast.h"
#include "token.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>


static Token* cur(Parser* p) {
    if (p->pos >= p->count) return &p->tokens[p->count - 1];
    return &p->tokens[p->pos];
}

static Token* prev(Parser* p) {
    int i = p->pos - 1;
    if (i < 0) i = 0;
    return &p->tokens[i];
}


static bool at(Parser* p, TokenType t) { return cur(p)->type == t; }
static bool is_eof(Parser* p) { return at(p, TOK_EOF); }

static void parser_add_error(Parser* p, const char* fmt, ...) {
    if (p->err_count >= p->err_cap) {
        int ncap = (p->err_cap == 0) ? 16 : p->err_cap * 2;
        char** nerrs = (char**)realloc(p->errors, (size_t)ncap * sizeof(char*));
        if (!nerrs) return;
        p->errors = nerrs;
        p->err_cap = ncap;
    }

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    // attach position (use current token)
    char full[640];
    Token* t = cur(p);
    snprintf(full, sizeof(full), "L%d:C%d: %s (token=%s '%s')",
             t->ligne, t->colonne, msg, token_to_string(t->type),
             t->valeur ? t->valeur : "");

    size_t n = strlen(full);
    char* s = (char*)malloc(n + 1);
    if (!s) return;
    memcpy(s, full, n + 1);
    p->errors[p->err_count++] = s;
}

static bool match(Parser* p, TokenType t) {
    if (at(p, t)) { p->pos++; return true; }
    return false;
}

static bool expect(Parser* p, TokenType t, const char* msg) {
    if (match(p, t)) return true;
    parser_add_error(p, "%s", msg);
    return false;
}

static void skip_fin_instr(Parser* p) {
    while (at(p, TOK_FIN_INSTR) || at(p, TOK_COMMENTAIRE) || at(p, TOK_COMMENTAIRES)) {
        p->pos++;
    }
}

static bool is_start_of_def(Parser* p) {
    return at(p, TOK_STRUCTURE) || at(p, TOK_FONCTION) || at(p, TOK_PROCEDURE);
}

static bool is_start_of_stmt(Parser* p) {
    TokenType t = cur(p)->type;
    return t == TOK_ID ||
           t == TOK_SI || t == TOK_POUR || t == TOK_TANTQUE || t == TOK_REPETER ||
           t == TOK_ECRIRE || t == TOK_LIRE ||
           t == TOK_RETOUR || t == TOK_RETOURNER ||
           t == TOK_SORTIR || t == TOK_QUITTER_POUR ||
           t == TOK_SELON;
}

// Retourner: on veut savoir si "pas d'expression" (retour vide) est acceptable
static bool is_return_terminator(Parser* p) {
    TokenType t = cur(p)->type;
    return t == TOK_FIN_INSTR || t == TOK_FIN || t == TOK_FIN_PROC || t == TOK_FIN_FONCT ||
           t == TOK_FIN_SI || t == TOK_SINON || t == TOK_SINONSI ||
           t == TOK_FINTANTQUE || t == TOK_FIN_POUR || t == TOK_FIN_SELON ||
           t == TOK_CAS || t == TOK_DEFAUT ||
           t == TOK_EOF;
}



static ASTNode* parse_declaration(Parser* p);
static ASTNode* parse_type(Parser* p);

static void parse_optional_local_objets(Parser* p, ASTList* out_decls);
static ASTNode* prepend_decls_to_block(ASTList* decls, ASTNode* body);

static ASTNode* parse_def_struct(Parser* p);
static ASTNode* parse_def_func(Parser* p);
static ASTNode* parse_def_proc(Parser* p);
static ASTNode* parse_param(Parser* p);

static ASTNode* parse_block_until(Parser* p, TokenType stop1, TokenType stop2, TokenType stop3);

static ASTNode* parse_statement(Parser* p);

static ASTNode* parse_stmt_if(Parser* p);
static ASTNode* parse_stmt_while(Parser* p);
static ASTNode* parse_stmt_for(Parser* p);
static ASTNode* parse_stmt_repeat(Parser* p);
static ASTNode* parse_stmt_write(Parser* p);
static ASTNode* parse_stmt_read(Parser* p);
static ASTNode* parse_stmt_return(Parser* p);
static ASTNode* parse_stmt_switch(Parser* p);

// IMPORTANT: pour gérer RemplirMatrice() / f(x) en instruction
static ASTNode* parse_stmt_starting_with_id(Parser* p);

static ASTNode* parse_lvalue(Parser* p);
static ASTNode* parse_expression(Parser* p);

// precedence levels
static ASTNode* parse_expr_or(Parser* p);
static ASTNode* parse_expr_and(Parser* p);
static ASTNode* parse_expr_cmp(Parser* p);
static ASTNode* parse_expr_add(Parser* p);
static ASTNode* parse_expr_mul(Parser* p);
static ASTNode* parse_expr_pow(Parser* p);
static ASTNode* parse_expr_unary(Parser* p);
static ASTNode* parse_expr_postfix(Parser* p);
static ASTNode* parse_expr_primary(Parser* p);


// Parser API

void parser_init(Parser* p, Token* tokens, int count) {
    p->tokens = tokens;
    p->count = count;
    p->pos = 0;
    p->errors = NULL;
    p->err_count = 0;
    p->err_cap = 0;
}

void parser_free(Parser* p) {
    for (int i = 0; i < p->err_count; i++) free(p->errors[i]);
    free(p->errors);
    p->errors = NULL;
    p->err_count = 0;
    p->err_cap = 0;
}


// Grammar

ASTNode* parse_program(Parser* p) {
    // Algorithme ID
    if (!expect(p, TOK_ALGORITHME, "Mot-clé 'Algorithme' attendu")) return NULL;

    Token* nameTok = cur(p);
    if (!expect(p, TOK_ID, "Nom d'algorithme (ID) attendu")) return NULL;

    ASTNode* prog = ast_new_program(nameTok->valeur, nameTok->ligne, nameTok->colonne);
    skip_fin_instr(p);

    // Optional Objets:
    if (match(p, TOK_OBJETS)) {
        expect(p, TOK_DEUX_POINTS, "':' attendu après 'Objets'");
        skip_fin_instr(p);

        while (!is_eof(p) && !at(p, TOK_DEBUT)) {
            skip_fin_instr(p);
            if (at(p, TOK_DEBUT) || is_eof(p)) break;

            ASTNode* d = parse_declaration(p);
            if (d) ast_program_add_decl(prog, d);

            skip_fin_instr(p);
        }
    }

    expect(p, TOK_DEBUT, "'Début' attendu");
    skip_fin_instr(p);

    // defs after Début
    while (!is_eof(p) && is_start_of_def(p)) {
        ASTNode* def = NULL;
        if (at(p, TOK_STRUCTURE)) def = parse_def_struct(p);
        else if (at(p, TOK_FONCTION)) def = parse_def_func(p);
        else if (at(p, TOK_PROCEDURE)) def = parse_def_proc(p);

        if (def) ast_program_add_def(prog, def);
        skip_fin_instr(p);
    }

    // main block until FIN
    ASTNode* mainb = ast_new_block(cur(p)->ligne, cur(p)->colonne);
    while (!is_eof(p) && !at(p, TOK_FIN)) {
        skip_fin_instr(p);
        if (at(p, TOK_FIN) || is_eof(p)) break;

        if (!is_start_of_stmt(p)) {
            parser_add_error(p, "Instruction attendue");
            p->pos++; // advance
            continue;
        }

        ASTNode* st = parse_statement(p);
        if (st) ast_block_add(mainb, st);
        skip_fin_instr(p);
    }
    prog->as.program.main_block = mainb;

    expect(p, TOK_FIN, "'Fin' attendu");
    skip_fin_instr(p);

    expect(p, TOK_EOF, "EOF attendu");
    return prog;
}

// Declarations

static ASTNode* parse_declaration(Parser* p) {
    // name ':' (Variable Type | Constante Type '=' expr | Tableau Type dims)
    Token* nameTok = cur(p);
    if (!expect(p, TOK_ID, "Nom (ID) attendu dans déclaration")) return NULL;

    if (!expect(p, TOK_DEUX_POINTS, "':' attendu après le nom de déclaration")) return NULL;

    int line = nameTok->ligne, col = nameTok->colonne;

    if (match(p, TOK_VARIABLE)) {
        ASTNode* t = parse_type(p);
        return ast_new_decl_var(nameTok->valeur, t, line, col);
    }

    if (match(p, TOK_CONSTANTE)) {
        ASTNode* t = parse_type(p);
        expect(p, TOK_EGAL, "'=' attendu dans déclaration de constante");
        ASTNode* v = parse_expression(p);
        return ast_new_decl_const(nameTok->valeur, t, v, line, col);
    }

    if (match(p, TOK_TABLEAU)) {
        ASTNode* elem = parse_type(p);
        ASTNode* arr = ast_new_decl_array(nameTok->valeur, elem, line, col);

        // dims: [expr]+
        int dims = 0;
        while (match(p, TOK_CROCHET_OUVRANT)) {
            ASTNode* dim = parse_expression(p);
            expect(p, TOK_CROCHET_FERMANT, "']' attendu");
            ast_list_push(&arr->as.decl_array.dims, dim);
            dims++;
        }
        if (dims == 0) {
            parser_add_error(p, "Tableau: au moins une dimension [taille] est requise");
        }
        return arr;
    }

    parser_add_error(p, "Après ':', attendu: Variable / Constante / tableau");
    return NULL;
}

//  Objets: optionnel dans les fonctions/procédures (avant Début)
static void parse_optional_local_objets(Parser* p, ASTList* out_decls) {
    ast_list_init(out_decls);

    if (!match(p, TOK_OBJETS)) return;

    expect(p, TOK_DEUX_POINTS, "':' attendu après 'Objets'");
    skip_fin_instr(p);

    while (!is_eof(p) && !at(p, TOK_DEBUT)) {
        skip_fin_instr(p);
        if (at(p, TOK_DEBUT) || is_eof(p)) break;

        ASTNode* d = parse_declaration(p);
        if (d) ast_list_push(out_decls, d);

        skip_fin_instr(p);
    }
}

static ASTNode* prepend_decls_to_block(ASTList* decls, ASTNode* body) {
    if (!body || body->kind != AST_BLOCK || !decls || decls->count == 0) return body;

    ASTNode* merged = ast_new_block(body->line, body->col);

    // 1) déclarations d’abord
    for (int i = 0; i < decls->count; i++) {
        ast_block_add(merged, decls->items[i]);
    }

    // 2) puis les instructions du body
    for (int i = 0; i < body->as.block.stmts.count; i++) {
        ast_block_add(merged, body->as.block.stmts.items[i]);
    }

    // libérer uniquement le conteneur de l'ancien bloc
    free(body->as.block.stmts.items);
    body->as.block.stmts.items = NULL;
    body->as.block.stmts.count = 0;
    body->as.block.stmts.cap = 0;
    free(body);

    return merged;
}

//  parse_type() accepte maintenant "Tableau entier[]" comme type paramètre
static ASTNode* parse_type(Parser* p) {
    Token* t = cur(p);
    int line = t->ligne, col = t->colonne;

    if (match(p, TOK_ENTIER))    return ast_new_type_primitive(TYPE_ENTIER, line, col);
    if (match(p, TOK_REEL))      return ast_new_type_primitive(TYPE_REEL, line, col);
    if (match(p, TOK_CARACTERE)) return ast_new_type_primitive(TYPE_CARACTERE, line, col);
    if (match(p, TOK_CHAINE))    return ast_new_type_primitive(TYPE_CHAINE, line, col);
    if (match(p, TOK_BOOLEEN))   return ast_new_type_primitive(TYPE_BOOLEEN, line, col);

    //  Type tableau
    if (match(p, TOK_TABLEAU)) {
        Token* kw = prev(p);
        ASTNode* elem = parse_type(p);
        ASTNode* arrT = ast_new_type_array(elem, kw->ligne, kw->colonne);

        int dims = 0;
        while (match(p, TOK_CROCHET_OUVRANT)) {
            // "[]" => dimension non fixée (paramètre)
            if (match(p, TOK_CROCHET_FERMANT)) {
                ast_list_push(&arrT->as.type_array.dims, NULL);
                dims++;
                continue;
            }
            ASTNode* dim = parse_expression(p);
            expect(p, TOK_CROCHET_FERMANT, "']' attendu");
            ast_list_push(&arrT->as.type_array.dims, dim);
            dims++;
        }

        if (dims == 0) {
            parser_add_error(p, "Type tableau: utiliser au moins une dimension [] ou [taille]");
        }
        return arrT;
    }

    // named type
    if (match(p, TOK_ID)) return ast_new_type_named(t->valeur, line, col);

    parser_add_error(p, "Type attendu (entier/réel/caractère/chaine/booléen ou ID)");
    return ast_new_type_named("<?>", line, col);
}

// Definitions

static ASTNode* parse_def_struct(Parser* p) {
    Token* kw = cur(p);
    expect(p, TOK_STRUCTURE, "'Structure' attendu");

    Token* name = cur(p);
    expect(p, TOK_ID, "Nom de structure (ID) attendu");

    ASTNode* st = ast_new_def_struct(name->valeur, kw->ligne, kw->colonne);
    skip_fin_instr(p);

    // fields: ID ':' Type FIN_INSTR*
    while (!is_eof(p) && !at(p, TOK_FIN_STRUCT)) {
        skip_fin_instr(p);
        if (at(p, TOK_FIN_STRUCT) || is_eof(p)) break;

        Token* fname = cur(p);
        if (!expect(p, TOK_ID, "Nom de champ (ID) attendu")) break;

        expect(p, TOK_DEUX_POINTS, "':' attendu après champ");
        ASTNode* ftype = parse_type(p);

        ASTNode* field = ast_new_field(fname->valeur, ftype, fname->ligne, fname->colonne);
        ast_list_push(&st->as.def_struct.fields, field);

        skip_fin_instr(p);
    }

    expect(p, TOK_FIN_STRUCT, "'Fin-struct' attendu");
    return st;
}

static ASTNode* parse_param(Parser* p) {
    Token* n = cur(p);
    expect(p, TOK_ID, "Nom paramètre (ID) attendu");
    expect(p, TOK_DEUX_POINTS, "':' attendu dans paramètre");
    ASTNode* t = parse_type(p);
    return ast_new_param(n->valeur, t, n->ligne, n->colonne);
}

static ASTNode* parse_def_func(Parser* p) {
    Token* kw = cur(p);
    expect(p, TOK_FONCTION, "'Fonction' attendu");

    Token* name = cur(p);
    expect(p, TOK_ID, "Nom de fonction (ID) attendu");

    ASTNode* fn = ast_new_def_func(name->valeur, NULL, kw->ligne, kw->colonne);

    // params
    expect(p, TOK_PAREN_OUVRANTE, "'(' attendu après nom de fonction");
    if (!at(p, TOK_PAREN_FERMANTE)) {
        ASTNode* pa = parse_param(p);
        ast_list_push(&fn->as.def_func.params, pa);
        while (match(p, TOK_VIRGULE)) {
            ASTNode* pb = parse_param(p);
            ast_list_push(&fn->as.def_func.params, pb);
        }
    }
    expect(p, TOK_PAREN_FERMANTE, "')' attendu");

    // return type: ':' Type
    expect(p, TOK_DEUX_POINTS, "':' attendu avant le type de retour");
    fn->as.def_func.return_type = parse_type(p);

    skip_fin_instr(p);

    // Objets: optionnel avant Début
    ASTList localDecls;
    parse_optional_local_objets(p, &localDecls);

    expect(p, TOK_DEBUT, "'Début' attendu dans fonction");
    skip_fin_instr(p);

    ASTNode* body = parse_block_until(p, TOK_FIN_FONCT, TOK_EOF, TOK_EOF);
    body = prepend_decls_to_block(&localDecls, body);

    ast_list_free_shallow(&localDecls);

    fn->as.def_func.body = body;

    expect(p, TOK_FIN_FONCT, "'FinFonct' attendu");
    return fn;
}

static ASTNode* parse_def_proc(Parser* p) {
    Token* kw = cur(p);
    expect(p, TOK_PROCEDURE, "'Procédure' attendu");

    Token* name = cur(p);
    expect(p, TOK_ID, "Nom de procédure (ID) attendu");

    ASTNode* pr = ast_new_def_proc(name->valeur, kw->ligne, kw->colonne);

    // params
    expect(p, TOK_PAREN_OUVRANTE, "'(' attendu après nom de procédure");
    if (!at(p, TOK_PAREN_FERMANTE)) {
        ASTNode* pa = parse_param(p);
        ast_list_push(&pr->as.def_proc.params, pa);
        while (match(p, TOK_VIRGULE)) {
            ASTNode* pb = parse_param(p);
            ast_list_push(&pr->as.def_proc.params, pb);
        }
    }
    expect(p, TOK_PAREN_FERMANTE, "')' attendu");

    skip_fin_instr(p);

    // Objets: optionnel AVANT Début (fix)
    ASTList localDecls;
    parse_optional_local_objets(p, &localDecls);

    expect(p, TOK_DEBUT, "'Début' attendu dans procédure");
    skip_fin_instr(p);

    ASTNode* body = parse_block_until(p, TOK_FIN_PROC, TOK_EOF, TOK_EOF);
    body = prepend_decls_to_block(&localDecls, body);

    ast_list_free_shallow(&localDecls);

    pr->as.def_proc.body = body;

    expect(p, TOK_FIN_PROC, "'FinProc' attendu");
    return pr;
}

// Parse a block until a stop token (stop2/stop3 optional)
static ASTNode* parse_block_until(Parser* p, TokenType stop1, TokenType stop2, TokenType stop3) {
    ASTNode* b = ast_new_block(cur(p)->ligne, cur(p)->colonne);

    while (!is_eof(p) && !at(p, stop1) && !at(p, stop2) && !at(p, stop3)) {
        skip_fin_instr(p);
        if (at(p, stop1) || at(p, stop2) || at(p, stop3) || is_eof(p)) break;

        if (!is_start_of_stmt(p)) {
            parser_add_error(p, "Instruction attendue dans bloc");
            p->pos++;
            continue;
        }

        ASTNode* st = parse_statement(p);
        if (st) ast_block_add(b, st);
        skip_fin_instr(p);
    }
    return b;
}

// Statements

static ASTNode* parse_statement(Parser* p) {
    Token* t = cur(p);

    if (at(p, TOK_SI)) return parse_stmt_if(p);
    if (at(p, TOK_TANTQUE)) return parse_stmt_while(p);
    if (at(p, TOK_POUR)) return parse_stmt_for(p);
    if (at(p, TOK_REPETER)) return parse_stmt_repeat(p);
    if (at(p, TOK_ECRIRE)) return parse_stmt_write(p);
    if (at(p, TOK_LIRE)) return parse_stmt_read(p);
    if (at(p, TOK_RETOUR) || at(p, TOK_RETOURNER)) return parse_stmt_return(p);
    if (at(p, TOK_SORTIR)) { match(p, TOK_SORTIR); return ast_new_break(t->ligne, t->colonne); }
    if (at(p, TOK_QUITTER_POUR)) { match(p, TOK_QUITTER_POUR); return ast_new_quit_for(t->ligne, t->colonne); }
    if (at(p, TOK_SELON)) return parse_stmt_switch(p);

    // ID: assignment or call-statement (or invalid)
    if (at(p, TOK_ID)) {
        return parse_stmt_starting_with_id(p);
    }

    parser_add_error(p, "Instruction inconnue");
    p->pos++;
    return NULL;
}

// ---- IMPORTANT FIX HERE ----
static ASTNode* parse_stmt_starting_with_id(Parser* p) {
    Token* first = cur(p);   // TOK_ID
    int line = first->ligne;
    int col  = first->colonne;

    // Parse ID + postfix: .  []  ()
    ASTNode* expr = parse_expr_postfix(p);

    // 1) Affectation
    if (match(p, TOK_AFFECTATION)) {
        if (!(expr &&
              (expr->kind == AST_IDENT ||
               expr->kind == AST_FIELD_ACCESS ||
               expr->kind == AST_INDEX))) {
            parser_add_error(p, "Cible d'affectation invalide");
        }
        ASTNode* value = parse_expression(p);
        return ast_new_assign(expr, value, line, col);
    }

    // 2) Appel => instruction
    if (expr && expr->kind == AST_CALL) {
        return ast_new_call_stmt(expr, line, col);
    }

    // 3) Sinon invalide
    parser_add_error(p, "Instruction invalide: affectation '<-' ou appel attendu après ID");
    return expr; // debug
}

static ASTNode* parse_stmt_write(Parser* p) {
    Token* kw = cur(p);
    match(p, TOK_ECRIRE);
    ASTNode* w = ast_new_write(kw->ligne, kw->colonne);

    expect(p, TOK_PAREN_OUVRANTE, "'(' attendu après Ecrire");
    if (!at(p, TOK_PAREN_FERMANTE)) {
        ASTNode* e = parse_expression(p);
        ast_list_push(&w->as.write_stmt.args, e);
        while (match(p, TOK_VIRGULE)) {
            ASTNode* e2 = parse_expression(p);
            ast_list_push(&w->as.write_stmt.args, e2);
        }
    }
    expect(p, TOK_PAREN_FERMANTE, "')' attendu après Ecrire(...)");
    return w;
}

static ASTNode* parse_stmt_read(Parser* p) {
    Token* kw = cur(p);
    match(p, TOK_LIRE);
    ASTNode* r = ast_new_read(kw->ligne, kw->colonne);

    expect(p, TOK_PAREN_OUVRANTE, "'(' attendu après Lire");
    if (!at(p, TOK_PAREN_FERMANTE)) {
        ASTNode* lv = parse_lvalue(p);
        ast_list_push(&r->as.read_stmt.targets, lv);
        while (match(p, TOK_VIRGULE)) {
            ASTNode* lv2 = parse_lvalue(p);
            ast_list_push(&r->as.read_stmt.targets, lv2);
        }
    }
    expect(p, TOK_PAREN_FERMANTE, "')' attendu après Lire(...)");
    return r;
}

static ASTNode* parse_stmt_return(Parser* p) {
    Token* kw = cur(p);

    if (match(p, TOK_RETOURNER)) {
        ASTNode* v = parse_expression(p);
        return ast_new_return(v, kw->ligne, kw->colonne);
    }

    if (match(p, TOK_RETOUR)) {
        if (is_return_terminator(p)) {
            return ast_new_return(NULL, kw->ligne, kw->colonne);
        }
        ASTNode* v = parse_expression(p);
        return ast_new_return(v, kw->ligne, kw->colonne);
    }

    return NULL;
}

static ASTNode* parse_stmt_if(Parser* p) {
    Token* kw = cur(p);
    match(p, TOK_SI);

    ASTNode* cond = parse_expression(p);
    expect(p, TOK_ALORS, "'Alors' attendu");
    skip_fin_instr(p);

    ASTNode* then_block = parse_block_until(p, TOK_SINONSI, TOK_SINON, TOK_FIN_SI);
    ASTNode* ifn = ast_new_if(cond, then_block, kw->ligne, kw->colonne);

    while (match(p, TOK_SINONSI)) {
        ASTNode* ec = parse_expression(p);
        expect(p, TOK_ALORS, "'Alors' attendu après SinonSi");
        skip_fin_instr(p);

        ASTNode* eb = parse_block_until(p, TOK_SINONSI, TOK_SINON, TOK_FIN_SI);
        ast_list_push(&ifn->as.if_stmt.elif_conds, ec);
        ast_list_push(&ifn->as.if_stmt.elif_blocks, eb);
    }

    if (match(p, TOK_SINON)) {
        skip_fin_instr(p);
        ifn->as.if_stmt.else_block = parse_block_until(p, TOK_FIN_SI, TOK_EOF, TOK_EOF);
    }

    expect(p, TOK_FIN_SI, "'FinSi' attendu");
    return ifn;
}

static ASTNode* parse_stmt_while(Parser* p) {
    Token* kw = cur(p);
    match(p, TOK_TANTQUE);

    ASTNode* cond = parse_expression(p);
    skip_fin_instr(p);

    ASTNode* body = parse_block_until(p, TOK_FINTANTQUE, TOK_EOF, TOK_EOF);
    expect(p, TOK_FINTANTQUE, "'FinTantQue' attendu");
    return ast_new_while(cond, body, kw->ligne, kw->colonne);
}

static ASTNode* parse_stmt_for(Parser* p) {
    Token* kw = cur(p);
    match(p, TOK_POUR);

    Token* var = cur(p);
    expect(p, TOK_ID, "Variable de boucle attendue (ID)");

    expect(p, TOK_AFFECTATION, "'<-' attendu dans Pour");
    ASTNode* start = parse_expression(p);

    expect(p, TOK_JUSQUA, "'jusqu'à' attendu");
    ASTNode* end = parse_expression(p);

    ASTNode* step = NULL;
    if (match(p, TOK_PAS)) {
        step = parse_expression(p);
    }

    skip_fin_instr(p);

    ASTNode* body = parse_block_until(p, TOK_FIN_POUR, TOK_EOF, TOK_EOF);
    expect(p, TOK_FIN_POUR, "'FinPour' attendu");

    return ast_new_for(var->valeur, start, end, step, body, kw->ligne, kw->colonne);
}

static ASTNode* parse_stmt_repeat(Parser* p) {
    Token* kw = cur(p);
    match(p, TOK_REPETER);
    skip_fin_instr(p);

    ASTNode* body = parse_block_until(p, TOK_TANTQUE, TOK_EOF, TOK_EOF);

    ASTNode* until_cond = NULL;
    if (match(p, TOK_TANTQUE)) {
        until_cond = parse_expression(p);
    }

    return ast_new_repeat(body, until_cond, kw->ligne, kw->colonne);
}

// SELON / CAS / DEFAUT

static ASTNode* parse_stmt_switch(Parser* p) {
    Token* kw = cur(p);
    match(p, TOK_SELON);

    ASTNode* expr = parse_expression(p);
    skip_fin_instr(p);

    ASTNode* sw = ast_new_switch(expr, kw->ligne, kw->colonne);

    bool saw_case_or_default = false;

    while (!is_eof(p) && !at(p, TOK_FIN_SELON)) {
        skip_fin_instr(p);
        if (at(p, TOK_FIN_SELON) || is_eof(p)) break;

        if (match(p, TOK_CAS)) {
            saw_case_or_default = true;
            ASTNode* cas = ast_new_case(prev(p)->ligne, prev(p)->colonne);

            ASTNode* v1 = parse_expression(p);
            ast_list_push(&cas->as.case_stmt.values, v1);
            while (match(p, TOK_VIRGULE)) {
                ASTNode* vx = parse_expression(p);
                ast_list_push(&cas->as.case_stmt.values, vx);
            }

            expect(p, TOK_DEUX_POINTS, "':' attendu après Cas ...");
            skip_fin_instr(p);

            cas->as.case_stmt.body = parse_block_until(p, TOK_CAS, TOK_DEFAUT, TOK_FIN_SELON);
            ast_list_push(&sw->as.switch_stmt.cases, cas);
            continue;
        }

        if (match(p, TOK_DEFAUT)) {
            saw_case_or_default = true;
            expect(p, TOK_DEUX_POINTS, "':' attendu après Défaut");
            skip_fin_instr(p);

            sw->as.switch_stmt.default_block = parse_block_until(p, TOK_FIN_SELON, TOK_EOF, TOK_EOF);
            continue;
        }

        parser_add_error(p, "Dans Selon: attendu 'Cas', 'Défaut' ou 'FinSelon'");
        p->pos++;
    }

    if (!saw_case_or_default) {
        parser_add_error(p, "Selon: au moins un Cas ou Défaut est attendu");
    }

    expect(p, TOK_FIN_SELON, "'FinSelon' attendu");
    return sw;
}

// Lvalue + expressions

static ASTNode* parse_lvalue(Parser* p) {
    Token* id = cur(p);
    expect(p, TOK_ID, "ID attendu");

    ASTNode* base = ast_new_ident(id->valeur, id->ligne, id->colonne);

    while (true) {
        if (match(p, TOK_CROCHET_OUVRANT)) {
            ASTNode* idx = parse_expression(p);
            expect(p, TOK_CROCHET_FERMANT, "']' attendu");
            base = ast_new_index(base, idx, id->ligne, id->colonne);
            continue;
        }
        if (match(p, TOK_POINT)) {
            Token* fld = cur(p);
            expect(p, TOK_ID, "Nom de champ attendu après '.'");
            base = ast_new_field_access(base, fld->valeur, fld->ligne, fld->colonne);
            continue;
        }
        break;
    }

    return base;
}

static ASTNode* parse_expression(Parser* p) { return parse_expr_or(p); }

static ASTNode* parse_expr_or(Parser* p) {
    ASTNode* left = parse_expr_and(p);
    while (match(p, TOK_OU)) {
        Token* op = prev(p);
        ASTNode* right = parse_expr_and(p);
        left = ast_new_binary(op->type, left, right, op->ligne, op->colonne);
    }
    return left;
}

static ASTNode* parse_expr_and(Parser* p) {
    ASTNode* left = parse_expr_cmp(p);
    while (match(p, TOK_ET)) {
        Token* op = prev(p);
        ASTNode* right = parse_expr_cmp(p);
        left = ast_new_binary(op->type, left, right, op->ligne, op->colonne);
    }
    return left;
}

static bool is_cmp(TokenType t) {
    return t == TOK_EGAL || t == TOK_DIFFERENT ||
           t == TOK_INFERIEUR || t == TOK_INFERIEUR_EGAL ||
           t == TOK_SUPERIEUR || t == TOK_SUPERIEUR_EGAL;
}

static ASTNode* parse_expr_cmp(Parser* p) {
    ASTNode* left = parse_expr_add(p);
    while (is_cmp(cur(p)->type)) {
        Token* op = cur(p);
        p->pos++;
        ASTNode* right = parse_expr_add(p);
        left = ast_new_binary(op->type, left, right, op->ligne, op->colonne);
    }
    return left;
}

static ASTNode* parse_expr_add(Parser* p) {
    ASTNode* left = parse_expr_mul(p);
    while (at(p, TOK_PLUS) || at(p, TOK_MOINS)) {
        Token* op = cur(p);
        p->pos++;
        ASTNode* right = parse_expr_mul(p);
        left = ast_new_binary(op->type, left, right, op->ligne, op->colonne);
    }
    return left;
}

static ASTNode* parse_expr_mul(Parser* p) {
    ASTNode* left = parse_expr_pow(p);
    while (at(p, TOK_FOIS) || at(p, TOK_DIVISE) || at(p, TOK_DIV_ENTIER) || at(p, TOK_MODULO)) {
        Token* op = cur(p);
        p->pos++;
        ASTNode* right = parse_expr_pow(p);
        left = ast_new_binary(op->type, left, right, op->ligne, op->colonne);
    }
    return left;
}

static ASTNode* parse_expr_pow(Parser* p) {
    ASTNode* left = parse_expr_unary(p);
    while (match(p, TOK_PUISSANCE)) {
        Token* op = prev(p);
        ASTNode* right = parse_expr_unary(p);
        left = ast_new_binary(op->type, left, right, op->ligne, op->colonne);
    }
    return left;
}

static ASTNode* parse_expr_unary(Parser* p) {
    if (match(p, TOK_NON)) {
        Token* op = prev(p);
        ASTNode* e = parse_expr_unary(p);
        return ast_new_unary(op->type, e, op->ligne, op->colonne);
    }
    if (match(p, TOK_MOINS)) {
        Token* op = prev(p);
        ASTNode* e = parse_expr_unary(p);
        return ast_new_unary(op->type, e, op->ligne, op->colonne);
    }
    return parse_expr_postfix(p);
}

static ASTNode* parse_expr_postfix(Parser* p) {
    ASTNode* base = parse_expr_primary(p);

    while (true) {
        // index
        if (match(p, TOK_CROCHET_OUVRANT)) {
            Token* br = prev(p);
            ASTNode* idx = parse_expression(p);
            expect(p, TOK_CROCHET_FERMANT, "']' attendu");
            base = ast_new_index(base, idx, br->ligne, br->colonne);
            continue;
        }
        // field access
        if (match(p, TOK_POINT)) {
            Token* fld = cur(p);
            expect(p, TOK_ID, "Nom de champ attendu après '.'");
            base = ast_new_field_access(base, fld->valeur, fld->ligne, fld->colonne);
            continue;
        }
        // call
        if (match(p, TOK_PAREN_OUVRANTE)) {
            Token* lp = prev(p);
            ASTNode* call = ast_new_call(base, lp->ligne, lp->colonne);

            if (!at(p, TOK_PAREN_FERMANTE)) {
                ASTNode* a1 = parse_expression(p);
                ast_list_push(&call->as.call.args, a1);
                while (match(p, TOK_VIRGULE)) {
                    ASTNode* ax = parse_expression(p);
                    ast_list_push(&call->as.call.args, ax);
                }
            }
            expect(p, TOK_PAREN_FERMANTE, "')' attendu");
            base = call;
            continue;
        }
        break;
    }

    return base;
}

static ASTNode* parse_expr_primary(Parser* p) {
    Token* t = cur(p);

    if (match(p, TOK_CONST_ENTIERE)) {
        long long v = 0;
        if (t->valeur) v = atoll(t->valeur);
        return ast_new_lit_int(v, t->ligne, t->colonne);
    }
    if (match(p, TOK_CONST_REEL)) {
        return ast_new_lit_real(t->valeur ? t->valeur : "0", t->ligne, t->colonne);
    }
    if (match(p, TOK_CONST_CHAINE)) {
        return ast_new_lit_string(t->valeur ? t->valeur : "", t->ligne, t->colonne);
    }
    if (match(p, TOK_VRAI)) {
        return ast_new_lit_bool(true, t->ligne, t->colonne);
    }
    if (match(p, TOK_FAUX)) {
        return ast_new_lit_bool(false, t->ligne, t->colonne);
    }
    if (match(p, TOK_ID)) {
        return ast_new_ident(t->valeur, t->ligne, t->colonne);
    }
    if (match(p, TOK_PAREN_OUVRANTE)) {
        ASTNode* e = parse_expression(p);
        expect(p, TOK_PAREN_FERMANTE, "')' attendu");
        return e;
    }

    parser_add_error(p, "Expression attendue");
    p->pos++;
    return ast_new_ident("<?>", t->ligne, t->colonne);
}
