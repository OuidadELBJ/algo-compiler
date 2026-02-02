#ifndef JGEN_H
#define JGEN_H

#include <stdbool.h>
#include "ast.h"

bool jgen_generate(ASTNode* program, const char* out_path);

#endif
