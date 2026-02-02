#ifndef TOKENS_H
#define TOKENS_H

#include <stdbool.h>

// Énumération des tokens (normaux et erreur)
typedef enum {
    // 1. Mots-clés de structure
    TOK_ALGORITHME, TOK_ALGORITHME_ERR,
    TOK_DEBUT, TOK_DEBUT_ERR,
    TOK_FIN, TOK_FIN_ERR,

    // 2. Déclarations, types et constantes
    TOK_OBJETS, TOK_OBJETS_ERR,
    TOK_VARIABLE, TOK_VARIABLE_ERR,
    TOK_CONSTANTE, TOK_CONSTANTE_ERR,
    TOK_ENTIER, TOK_ENTIER_ERR,
    TOK_REEL, TOK_REEL_ERR,
    TOK_CARACTERE, TOK_CARACTERE_ERR,
    TOK_CHAINE, TOK_CHAINE_ERR,
    TOK_BOOLEEN, TOK_BOOLEEN_ERR,
    TOK_CONST_ENTIERE, TOK_CONST_ENTIERE_ERR,
    TOK_CONST_REEL, TOK_CONST_REEL_ERR,
    TOK_CONST_CHAINE, TOK_CONST_CHAINE_ERR,
    TOK_ID, TOK_ID_ERR,
    TOK_TABLEAU, TOK_TABLEAU_ERR,
    TOK_DE, TOK_DE_ERR,
    TOK_STRUCTURE, TOK_STRUCTURE_ERR,
    TOK_FIN_STRUCT, TOK_FIN_STRUCT_ERR,

    // 3. Entrées / sorties
    TOK_ECRIRE, TOK_ECRIRE_ERR,
    TOK_LIRE, TOK_LIRE_ERR,
    TOK_RETOUR, TOK_RETOUR_ERR,

    // 4. Constantes logiques et opérateurs logiques
    TOK_VRAI, TOK_VRAI_ERR,
    TOK_FAUX, TOK_FAUX_ERR,
    TOK_ET, TOK_ET_ERR,
    TOK_OU, TOK_OU_ERR,
    TOK_NON, TOK_NON_ERR,

    // 5. Comparateurs
    TOK_INFERIEUR, TOK_INFERIEUR_ERR,
    TOK_INFERIEUR_EGAL, TOK_INFERIEUR_EGAL_ERR,
    TOK_SUPERIEUR, TOK_SUPERIEUR_ERR,
    TOK_SUPERIEUR_EGAL, TOK_SUPERIEUR_EGAL_ERR,
    TOK_EGAL, TOK_EGAL_ERR,
    TOK_DIFFERENT, TOK_DIFFERENT_ERR,

    // 6. Affectation, séparateurs, ponctuation
    TOK_AFFECTATION, TOK_AFFECTATION_ERR,
    TOK_DEUX_POINTS, TOK_DEUX_POINTS_ERR,
    TOK_VIRGULE, TOK_VIRGULE_ERR,
    TOK_PAREN_OUVRANTE, TOK_PAREN_OUVRANTE_ERR,
    TOK_PAREN_FERMANTE, TOK_PAREN_FERMANTE_ERR,
    TOK_CROCHET_OUVRANT, TOK_CROCHET_OUVRANT_ERR,
    TOK_CROCHET_FERMANT, TOK_CROCHET_FERMANT_ERR,
    TOK_GUILLEMET, TOK_GUILLEMET_ERR,
    TOK_POINT, TOK_POINT_ERR,
    TOK_FIN_INSTR, TOK_FIN_INSTR_ERR,

    // 7. Opérateurs arithmétiques
    TOK_PLUS, TOK_PLUS_ERR,
    TOK_MOINS, TOK_MOINS_ERR,
    TOK_FOIS, TOK_FOIS_ERR,
    TOK_DIVISE, TOK_DIVISE_ERR,
    TOK_DIV_ENTIER, TOK_DIV_ENTIER_ERR,
    TOK_MODULO, TOK_MODULO_ERR,
    TOK_PUISSANCE, TOK_PUISSANCE_ERR,

    // 8. Structures de contrôle
    TOK_SI, TOK_SI_ERR,
    TOK_SINONSI, TOK_SINONSI_ERR,
    TOK_ALORS, TOK_ALORS_ERR,
    TOK_SINON, TOK_SINON_ERR,
    TOK_FIN_SI, TOK_FIN_SI_ERR,
    TOK_SELON, TOK_SELON_ERR,
    TOK_CAS, TOK_CAS_ERR,
    TOK_DEFAUT, TOK_DEFAUT_ERR,
    TOK_FIN_SELON, TOK_FIN_SELON_ERR,
    TOK_SORTIR, TOK_SORTIR_ERR,
    TOK_POUR, TOK_POUR_ERR,
    TOK_JUSQUA, TOK_JUSQUA_ERR,
    TOK_REPETER, TOK_REPETER_ERR,
    TOK_PAS, TOK_PAS_ERR,
    TOK_FIN_POUR, TOK_FIN_POUR_ERR,
    TOK_QUITTER_POUR, TOK_QUITTER_POUR_ERR,
    TOK_TANTQUE, TOK_TANTQUE_ERR,
    TOK_FINTANTQUE, TOK_FINTANTQUE_ERR,

    // 9. Procédures et fonctions
    TOK_PROCEDURE, TOK_PROCEDURE_ERR,
    TOK_FIN_PROC, TOK_FIN_PROC_ERR,
    TOK_FONCTION, TOK_FONCTION_ERR,
    TOK_FIN_FONCT, TOK_FIN_FONCT_ERR,
    TOK_RETOURNER, TOK_RETOURNER_ERR,

    // 10. Autres tokens spéciaux
    TOK_EOF, TOK_EOF_ERR,
    TOK_COMMENTAIRE, TOK_COMMENTAIRE_ERR,
    TOK_COMMENTAIRES, TOK_COMMENTAIRES_ERR
} TokenType;

// Structure d'un token
typedef struct {
    TokenType type;
    char* valeur;
    int ligne;
    int colonne;
} Token;

// Prototypes des fonctions
Token* creer_token(TokenType type, const char* valeur, int ligne, int colonne);
void detruire_token(Token* token);
Token* copier_token(const Token* token);

const char* token_to_string(TokenType type);
void afficher_token_ligne(const Token* token);
void afficher_token_compact(const Token* token);
void afficher_token_detail(const Token* token);

bool est_token_erreur(TokenType type);
bool est_mot_cle(TokenType type);
bool est_operateur(TokenType type);
bool est_separateur(TokenType type);
bool est_constante(TokenType type);
bool est_type_donnee(TokenType type);

bool tokens_egaux(const Token* t1, const Token* t2);
bool tokens_types_egaux(const Token* t1, const Token* t2);
bool tokens_valeurs_egales(const Token* t1, const Token* t2);

bool token_valeur_vide(const Token* token);
bool token_valide(const Token* token);

#endif