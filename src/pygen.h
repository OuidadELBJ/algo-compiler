#ifndef PYGEN_H
#define PYGEN_H

#include <stdbool.h>
#include "ast.h"


bool pygen_generate(ASTNode *program, const char *output_path);

#endif 