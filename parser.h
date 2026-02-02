#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "ast.h"

typedef struct {
    Token* tokens;
    int count;
    int pos;

    char** errors;
    int err_count;
    int err_cap;
} Parser;

void parser_init(Parser* p, Token* tokens, int count);
void parser_free(Parser* p);

ASTNode* parse_program(Parser* p);

#endif
