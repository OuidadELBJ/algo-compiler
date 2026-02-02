#ifndef IR_H
#define IR_H

// P-CODE / IR (bas niveau) generator
//
// IR pensé pour une traduction simple vers du C.
// Machine à pile, avec instructions principales :
//
//  - LDA <name>   : push adresse
//  - LDI <int>    : push entier immédiat
//  - LDR <real>   : push réel texte
//  - LDS "..."    : push string immédiat
//  - LDV          : charge la valeur à l'adresse au sommet de pile
//  - STO          : store (addr, val -> )
//  - IDX          : addr = base[index]
//  - FLD <off>    : addr = base + offset
//  - FLDNAME <id> : fallback symbolique si offset de champ inconnu
//
//  - ADD SUB MUL DIV IDIV MOD POW
//  - EQ NE LT LE GT GE
//  - AND OR NOT NEG
//
//  - JMP Lx, JZ Lx, JNZ Lx
//  - CALL name argc
//  - RET / RETV
//  - POP DUP
//  - BRK / QUITFOR (placeholders si VM les supporte)
//  - HLT
//
//  - Impression typée (en fonction de l'ExprType) :
//      PRNI / PRNR / PRNB / PRNC / PRS
//    => convention : on empile d'abord la valeur, puis on appelle PRN*.
//
//  - Directives pour aider la traduction vers le C :
//      .program, .globals/.endglobals
//      .proc/.endproc, .func/.endfunc
//      .main/.endmain
//      VAR/CONST/ARRAY/PARAM/LOCAL/LOCAL_ARRAY/LOCAL_CONST (+ type imprimé)

#include <stdio.h>
#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IRProgram IRProgram;

// Génère un IR/P-code à partir de l’AST racine (AST_PROGRAM).
IRProgram* ir_generate(ASTNode* program);

// Affiche l’IR dans un fichier (stdout si out == NULL).
void ir_print(IRProgram* prog, FILE* out);

// Libère la structure IR.
void ir_free(IRProgram* prog);

// Compat : ancien nom possible
static inline void ir_program_print(IRProgram* p, FILE* out) { ir_print(p, out); }
static inline void ir_program_free(IRProgram* p) { ir_free(p); }

#ifdef __cplusplus
}
#endif

#endif // IR_H