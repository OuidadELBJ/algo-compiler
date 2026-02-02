#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>
#include "token.h"

typedef struct {
    const char* source;
    int position;
    int ligne;
    int colonne;

    Token* tokens;
    int nb_tokens;
    int capacite_tokens;

    char** messages_erreur;
    int nb_erreurs;
    int capacite_erreurs;

    char* nom_fichier;
    bool mode_strict;

    // AJOUTS (pour gérer FIN_INSTR correctement)
    int paren_depth;    // profondeur des parenthèses ()
    int bracket_depth;  // profondeur des crochets []
} Lexer;

// API
Lexer* creer_lexer(const char* source, const char* nom_fichier);
void detruire_lexer(Lexer* lexer);

bool analyser_lexicalement(Lexer* lexer);

Token* obtenir_tokens(Lexer* lexer, int* nb_tokens);
char** obtenir_messages_erreur(Lexer* lexer, int* nb_erreurs);

void afficher_token(Token* token);
void afficher_tokens(Lexer* lexer);
void afficher_erreurs(Lexer* lexer);
int compter_tokens_erreur(Lexer* lexer);

void set_mode_strict(Lexer* lexer, bool strict);

#endif
