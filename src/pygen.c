#include "pygen.h"
#include "token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>



typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Str;

static void str_init(Str* s) {
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

static void str_grow(Str* s, size_t add) {
    if (s->len + add + 1 > s->cap) {
        size_t nc = (s->cap == 0) ? 1024 : s->cap * 2;
        while (nc < s->len + add + 1) nc *= 2;
        s->data = realloc(s->data, nc);
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

static void str_free(Str* s) {
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

/* ============================================================
   Générateur Python
   ============================================================ */

typedef struct {
    Str out;
    int indent;
} PG;

static void emit_indent(PG* pg) {
    for (int i = 0; i < pg->indent; i++)
        str_append(&pg->out, "    ");
}

static void emit_ln(PG* pg, const char* s) {
    emit_indent(pg);
    str_append(&pg->out, s);
    str_append(&pg->out, "\n");
}

/* ============================================================
   Expressions
   ============================================================ */

static void emit_expr(PG* pg, ASTNode* e);

static void emit_binop(PG* pg, TokenType op) {
    switch (op) {
        case TOK_PLUS: str_append(&pg->out, " + "); break;
        case TOK_MOINS: str_append(&pg->out, " - "); break;
        case TOK_FOIS: str_append(&pg->out, " * "); break;
        case TOK_DIVISE: str_append(&pg->out, " / "); break;
        case TOK_MODULO: str_append(&pg->out, " % "); break;

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

static void emit_expr(PG* pg, ASTNode* e) {
    if (!e) { str_append(&pg->out, "None"); return; }

    switch (e->kind) {

        case AST_LITERAL_INT:
            str_printf(&pg->out, "%lld", e->as.lit_int.value);
            break;

        case AST_LITERAL_REAL:
            str_append(&pg->out, e->as.lit_real.text);
            break;

        case AST_LITERAL_BOOL:
            str_append(&pg->out, e->as.lit_bool.value ? "True" : "False");
            break;

        case AST_LITERAL_STRING: {
            str_append(&pg->out, "\"");
            for (char* p = e->as.lit_string.text; *p; p++) {
                if (*p == '\\') str_append(&pg->out, "\\\\");
                else if (*p == '"') str_append(&pg->out, "\\\"");
                else {
                    char tmp[2] = {*p, 0};
                    str_append(&pg->out, tmp);
                }
            }
            str_append(&pg->out, "\"");
            break;
        }

        case AST_IDENT:
            str_append(&pg->out, e->as.ident.name);
            break;

        case AST_UNARY:
            if (e->as.unary.op == TOK_NON) str_append(&pg->out, "not ");
            else if (e->as.unary.op == TOK_MOINS) str_append(&pg->out, "-");
            emit_expr(pg, e->as.unary.expr);
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

/* ============================================================
   Statements / Blocks
   ============================================================ */

static void emit_block(PG* pg, ASTNode* b);

static void emit_stmt(PG* pg, ASTNode* s) {
    if (!s) return;

    switch (s->kind) {

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
            emit_indent(pg);
            str_append(&pg->out, "print(");
            for (int i = 0; i < s->as.write_stmt.args.count; i++) {
                if (i > 0) str_append(&pg->out, ", ");
                emit_expr(pg, s->as.write_stmt.args.items[i]);
            }
            str_append(&pg->out, ")\n");
            break;

        case AST_READ:
            for (int i = 0; i < s->as.read_stmt.targets.count; i++) {
                emit_indent(pg);
                emit_expr(pg, s->as.read_stmt.targets.items[i]);
                str_append(&pg->out, " = input()\n");
            }
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

        case AST_FOR:
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
    if (!b || b->kind != AST_BLOCK) return;
    for (int i = 0; i < b->as.block.stmts.count; i++) {
        emit_stmt(pg, b->as.block.stmts.items[i]);
    }
}

/* ============================================================
   Programme
   ============================================================ */

bool pygen_generate(ASTNode* program, const char* output_path) {
    if (!program || program->kind != AST_PROGRAM) return false;

    PG pg;
    str_init(&pg.out);
    pg.indent = 0;

    str_append(&pg.out, "# Generated Python code\n\n");

    /* Fonctions / procédures */
    for (int i = 0; i < program->as.program.defs.count; i++) {
        ASTNode* d = program->as.program.defs.items[i];
        if (!d) continue;

        if (d->kind == AST_DEF_FUNC || d->kind == AST_DEF_PROC) {
            emit_indent(&pg);
            str_append(&pg.out, "def ");
            str_append(&pg.out, d->kind == AST_DEF_FUNC ? d->as.def_func.name : d->as.def_proc.name);
            str_append(&pg.out, "(");

            ASTList* params = (d->kind == AST_DEF_FUNC) ? &d->as.def_func.params : &d->as.def_proc.params;
            for (int p = 0; p < params->count; p++) {
                if (p > 0) str_append(&pg.out, ", ");
                str_append(&pg.out, params->items[p]->as.param.name);
            }
            str_append(&pg.out, "):\n");

            pg.indent++;
            emit_block(&pg, d->kind == AST_DEF_FUNC ? d->as.def_func.body : d->as.def_proc.body);
            pg.indent--;

            str_append(&pg.out, "\n");
        }
    }

    /* Main */
    str_append(&pg.out, "if __name__ == \"__main__\":\n");
    pg.indent++;
    emit_block(&pg, program->as.program.main_block);
    pg.indent--;

    FILE* f = fopen(output_path, "w");
    if (!f) {
        str_free(&pg.out);
        return false;
    }

    fputs(pg.out.data ? pg.out.data : "", f);
    fclose(f);
    str_free(&pg.out);
    return true;
}