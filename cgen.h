#ifndef CGEN_H
#define CGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "ast.h"

// Génère un fichier C complet à partir de l'AST du programme.
// Retourne true si OK.
bool cgen_generate(ASTNode* program, const char* output_c_path);

// Variante: écrit directement dans un FILE* déjà ouvert.
bool cgen_generate_to_file(ASTNode* program, FILE* out);

#endif
