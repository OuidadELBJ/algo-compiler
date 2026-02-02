#include "token.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// ============================================================================
// FONCTIONS DE GESTION DES TOKENS
// ============================================================================

// Crée un nouveau token
Token* creer_token(TokenType type, const char* valeur, int ligne, int colonne) {
    Token* token = (Token*)malloc(sizeof(Token));
    if (token == NULL) {
        fprintf(stderr, "Erreur d'allocation mémoire pour token\n");
        return NULL;
    }
    
    token->type = type;
    
    if (valeur != NULL) {
        token->valeur = strdup(valeur);
        if (token->valeur == NULL) {
            fprintf(stderr, "Erreur d'allocation mémoire pour valeur du token\n");
            free(token);
            return NULL;
        }
    } else {
        token->valeur = strdup("");
    }
    
    token->ligne = ligne;
    token->colonne = colonne;
    
    return token;
}

// Libère un token
void detruire_token(Token* token) {
    if (token != NULL) {
        free(token->valeur);
        free(token);
    }
}

// Crée une copie d'un token
Token* copier_token(const Token* token) {
    if (token == NULL) return NULL;
    
    return creer_token(token->type, token->valeur, token->ligne, token->colonne);
}

// ============================================================================
// FONCTIONS D'UTILITAIRE
// ============================================================================

// Vérifie si un token est un token d'erreur
bool est_token_erreur(TokenType type) {
    return (type % 2 == 1);
}

// Vérifie si un token est un mot-clé
bool est_mot_cle(TokenType type) {
    switch (type) {
        // 1. Mots-clés de structure
        case TOK_ALGORITHME:
        case TOK_DEBUT:
        case TOK_FIN:
        
        // 2. Déclarations, types et constantes
        case TOK_OBJETS:
        case TOK_VARIABLE:
        case TOK_CONSTANTE:
        case TOK_ENTIER:
        case TOK_REEL:
        case TOK_CARACTERE:
        case TOK_CHAINE:
        case TOK_BOOLEEN:
        case TOK_TABLEAU:
        case TOK_DE:
        case TOK_STRUCTURE:
        case TOK_FIN_STRUCT:
        
        // 3. Entrées / sorties
        case TOK_ECRIRE:
        case TOK_LIRE:
        case TOK_RETOUR:
        
        // 4. Constantes logiques et opérateurs logiques
        case TOK_VRAI:
        case TOK_FAUX:
        case TOK_ET:
        case TOK_OU:
        case TOK_NON:
        
        // 7. Opérateurs arithmétiques spéciaux
        case TOK_DIV_ENTIER:
        case TOK_MODULO:
        
        // 8. Structures de contrôle
        case TOK_SI:
        case TOK_SINONSI:
        case TOK_ALORS:
        case TOK_SINON:
        case TOK_FIN_SI:
        case TOK_SELON:
        case TOK_CAS:
        case TOK_DEFAUT:
        case TOK_FIN_SELON:
        case TOK_SORTIR:
        case TOK_POUR:
        case TOK_JUSQUA:
        case TOK_REPETER:
        case TOK_PAS:
        case TOK_FIN_POUR:
        case TOK_QUITTER_POUR:
        case TOK_TANTQUE:
        case TOK_FINTANTQUE:
        
        // 9. Procédures et fonctions
        case TOK_PROCEDURE:
        case TOK_FIN_PROC:
        case TOK_FONCTION:
        case TOK_FIN_FONCT:
        case TOK_RETOURNER:
            return true;
            
        default:
            return false;
    }
}

// Vérifie si un token est un opérateur
bool est_operateur(TokenType type) {
    switch (type) {
        // 5. Comparateurs
        case TOK_INFERIEUR:
        case TOK_INFERIEUR_EGAL:
        case TOK_SUPERIEUR:
        case TOK_SUPERIEUR_EGAL:
        case TOK_EGAL:
        case TOK_DIFFERENT:
        
        // 7. Opérateurs arithmétiques
        case TOK_PLUS:
        case TOK_MOINS:
        case TOK_FOIS:
        case TOK_DIVISE:
        case TOK_DIV_ENTIER:
        case TOK_MODULO:
        case TOK_PUISSANCE:
        
        // 4. Opérateurs logiques
        case TOK_ET:
        case TOK_OU:
        case TOK_NON:
        
        // 6. Affectation
        case TOK_AFFECTATION:
            return true;
            
        default:
            return false;
    }
}

// Vérifie si un token est un séparateur
bool est_separateur(TokenType type) {
    switch (type) {
        case TOK_DEUX_POINTS:
        case TOK_VIRGULE:
        case TOK_PAREN_OUVRANTE:
        case TOK_PAREN_FERMANTE:
        case TOK_CROCHET_OUVRANT:
        case TOK_CROCHET_FERMANT:
        case TOK_GUILLEMET:
        case TOK_POINT:
        case TOK_FIN_INSTR:
            return true;

        default:
            return false;
    }
}

// Vérifie si un token est une constante
bool est_constante(TokenType type) {
    switch (type) {
        case TOK_CONST_ENTIERE:
        case TOK_CONST_REEL:
        case TOK_CONST_CHAINE:
        case TOK_VRAI:
        case TOK_FAUX:
            return true;
            
        default:
            return false;
    }
}

// Vérifie si un token est un type de donnée
bool est_type_donnee(TokenType type) {
    switch (type) {
        case TOK_ENTIER:
        case TOK_REEL:
        case TOK_CARACTERE:
        case TOK_CHAINE:
        case TOK_BOOLEEN:
        case TOK_TABLEAU:
            return true;
            
        default:
            return false;
    }
}

// ============================================================================
// FONCTION DE CONVERSION TOKEN -> STRING
// ============================================================================

// Convertit un type de token en chaîne de caractères
const char* token_to_string(TokenType type) {
    switch (type) {
        // 1. Mots-clés de structure
        case TOK_ALGORITHME: return "ALGORITHME";
        case TOK_ALGORITHME_ERR: return "ALGORITHME_ERR";
        case TOK_DEBUT: return "DEBUT";
        case TOK_DEBUT_ERR: return "DEBUT_ERR";
        case TOK_FIN: return "FIN";
        case TOK_FIN_ERR: return "FIN_ERR";
        
        // 2. Déclarations, types et constantes
        case TOK_OBJETS: return "OBJETS";
        case TOK_OBJETS_ERR: return "OBJETS_ERR";
        case TOK_VARIABLE: return "VARIABLE";
        case TOK_VARIABLE_ERR: return "VARIABLE_ERR";
        case TOK_CONSTANTE: return "CONSTANTE";
        case TOK_CONSTANTE_ERR: return "CONSTANTE_ERR";
        case TOK_ENTIER: return "ENTIER";
        case TOK_ENTIER_ERR: return "ENTIER_ERR";
        case TOK_REEL: return "REEL";
        case TOK_REEL_ERR: return "REEL_ERR";
        case TOK_CARACTERE: return "CARACTERE";
        case TOK_CARACTERE_ERR: return "CARACTERE_ERR";
        case TOK_CHAINE: return "CHAINE";
        case TOK_CHAINE_ERR: return "CHAINE_ERR";
        case TOK_BOOLEEN: return "BOOLEEN";
        case TOK_BOOLEEN_ERR: return "BOOLEEN_ERR";
        case TOK_CONST_ENTIERE: return "CONST_ENTIERE";
        case TOK_CONST_ENTIERE_ERR: return "CONST_ENTIERE_ERR";
        case TOK_CONST_REEL: return "CONST_REEL";
        case TOK_CONST_REEL_ERR: return "CONST_REEL_ERR";
        case TOK_CONST_CHAINE: return "CONST_CHAINE";
        case TOK_CONST_CHAINE_ERR: return "CONST_CHAINE_ERR";
        case TOK_ID: return "ID";
        case TOK_ID_ERR: return "ID_ERR";
        case TOK_TABLEAU: return "TABLEAU";
        case TOK_TABLEAU_ERR: return "TABLEAU_ERR";
        case TOK_DE: return "DE";
        case TOK_DE_ERR: return "DE_ERR";
        case TOK_STRUCTURE: return "STRUCTURE";
        case TOK_STRUCTURE_ERR: return "STRUCTURE_ERR";
        case TOK_FIN_STRUCT: return "FIN_STRUCT";
        case TOK_FIN_STRUCT_ERR: return "FIN_STRUCT_ERR";
        
        // 3. Entrées / sorties
        case TOK_ECRIRE: return "ECRIRE";
        case TOK_ECRIRE_ERR: return "ECRIRE_ERR";
        case TOK_LIRE: return "LIRE";
        case TOK_LIRE_ERR: return "LIRE_ERR";
        case TOK_RETOUR: return "RETOUR";
        case TOK_RETOUR_ERR: return "RETOUR_ERR";
        
        // 4. Constantes logiques et opérateurs logiques
        case TOK_VRAI: return "VRAI";
        case TOK_VRAI_ERR: return "VRAI_ERR";
        case TOK_FAUX: return "FAUX";
        case TOK_FAUX_ERR: return "FAUX_ERR";
        case TOK_ET: return "ET";
        case TOK_ET_ERR: return "ET_ERR";
        case TOK_OU: return "OU";
        case TOK_OU_ERR: return "OU_ERR";
        case TOK_NON: return "NON";
        case TOK_NON_ERR: return "NON_ERR";
        
        // 5. Comparateurs
        case TOK_INFERIEUR: return "INFERIEUR";
        case TOK_INFERIEUR_ERR: return "INFERIEUR_ERR";
        case TOK_INFERIEUR_EGAL: return "INFERIEUR_EGAL";
        case TOK_INFERIEUR_EGAL_ERR: return "INFERIEUR_EGAL_ERR";
        case TOK_SUPERIEUR: return "SUPERIEUR";
        case TOK_SUPERIEUR_ERR: return "SUPERIEUR_ERR";
        case TOK_SUPERIEUR_EGAL: return "SUPERIEUR_EGAL";
        case TOK_SUPERIEUR_EGAL_ERR: return "SUPERIEUR_EGAL_ERR";
        case TOK_EGAL: return "EGAL";
        case TOK_EGAL_ERR: return "EGAL_ERR";
        case TOK_DIFFERENT: return "DIFFERENT";
        case TOK_DIFFERENT_ERR: return "DIFFERENT_ERR";
        
        // 6. Affectation, séparateurs, ponctuation
        case TOK_AFFECTATION: return "AFFECTATION";
        case TOK_AFFECTATION_ERR: return "AFFECTATION_ERR";
        case TOK_DEUX_POINTS: return "DEUX_POINTS";
        case TOK_DEUX_POINTS_ERR: return "DEUX_POINTS_ERR";
        case TOK_VIRGULE: return "VIRGULE";
        case TOK_VIRGULE_ERR: return "VIRGULE_ERR";
        case TOK_PAREN_OUVRANTE: return "PAREN_OUVRANTE";
        case TOK_PAREN_OUVRANTE_ERR: return "PAREN_OUVRANTE_ERR";
        case TOK_PAREN_FERMANTE: return "PAREN_FERMANTE";
        case TOK_PAREN_FERMANTE_ERR: return "PAREN_FERMANTE_ERR";
        case TOK_CROCHET_OUVRANT: return "CROCHET_OUVRANT";
        case TOK_CROCHET_OUVRANT_ERR: return "CROCHET_OUVRANT_ERR";
        case TOK_CROCHET_FERMANT: return "CROCHET_FERMANT";
        case TOK_CROCHET_FERMANT_ERR: return "CROCHET_FERMANT_ERR";
        case TOK_GUILLEMET: return "GUILLEMET";
        case TOK_GUILLEMET_ERR: return "GUILLEMET_ERR";
        case TOK_POINT: return "POINT";
        case TOK_POINT_ERR: return "POINT_ERR";
        case TOK_FIN_INSTR: return "FIN_INSTR";
        case TOK_FIN_INSTR_ERR: return "FIN_INSTR_ERR";
        
        // 7. Opérateurs arithmétiques
        case TOK_PLUS: return "PLUS";
        case TOK_PLUS_ERR: return "PLUS_ERR";
        case TOK_MOINS: return "MOINS";
        case TOK_MOINS_ERR: return "MOINS_ERR";
        case TOK_FOIS: return "FOIS";
        case TOK_FOIS_ERR: return "FOIS_ERR";
        case TOK_DIVISE: return "DIVISE";
        case TOK_DIVISE_ERR: return "DIVISE_ERR";
        case TOK_DIV_ENTIER: return "DIV_ENTIER";
        case TOK_DIV_ENTIER_ERR: return "DIV_ENTIER_ERR";
        case TOK_MODULO: return "MODULO";
        case TOK_MODULO_ERR: return "MODULO_ERR";
        case TOK_PUISSANCE: return "PUISSANCE";
        case TOK_PUISSANCE_ERR: return "PUISSANCE_ERR";
        
        // 8. Structures de contrôle
        case TOK_SI: return "SI";
        case TOK_SI_ERR: return "SI_ERR";
        case TOK_SINONSI: return "SINONSI";
        case TOK_SINONSI_ERR: return "SINONSI_ERR";
        case TOK_ALORS: return "ALORS";
        case TOK_ALORS_ERR: return "ALORS_ERR";
        case TOK_SINON: return "SINON";
        case TOK_SINON_ERR: return "SINON_ERR";
        case TOK_FIN_SI: return "FIN_SI";
        case TOK_FIN_SI_ERR: return "FIN_SI_ERR";
        case TOK_SELON: return "SELON";
        case TOK_CAS: return "CAS";
        case TOK_CAS_ERR: return "CAS_ERR";
        case TOK_DEFAUT: return "DEFAUT";
        case TOK_DEFAUT_ERR: return "DEFAUT_ERR";
        case TOK_SELON_ERR: return "SELON_ERR";
        case TOK_FIN_SELON: return "FIN_SELON";
        case TOK_FIN_SELON_ERR: return "FIN_SELON_ERR";
        case TOK_SORTIR: return "SORTIR";
        case TOK_SORTIR_ERR: return "SORTIR_ERR";
        case TOK_POUR: return "POUR";
        case TOK_POUR_ERR: return "POUR_ERR";
        case TOK_JUSQUA: return "JUSQUA";
        case TOK_JUSQUA_ERR: return "JUSQUA_ERR";
        case TOK_REPETER: return "REPETER";
        case TOK_REPETER_ERR: return "REPETER_ERR";
        case TOK_PAS: return "PAS";
        case TOK_PAS_ERR: return "PAS_ERR";
        case TOK_FIN_POUR: return "FIN_POUR";
        case TOK_FIN_POUR_ERR: return "FIN_POUR_ERR";
        case TOK_QUITTER_POUR: return "QUITTER_POUR";
        case TOK_QUITTER_POUR_ERR: return "QUITTER_POUR_ERR";
        case TOK_TANTQUE: return "TANTQUE";
        case TOK_TANTQUE_ERR: return "TANTQUE_ERR";
        case TOK_FINTANTQUE: return "FINTANTQUE";
        case TOK_FINTANTQUE_ERR: return "FINTANTQUE_ERR";
        
        // 9. Procédures et fonctions
        case TOK_PROCEDURE: return "PROCEDURE";
        case TOK_PROCEDURE_ERR: return "PROCEDURE_ERR";
        case TOK_FIN_PROC: return "FIN_PROC";
        case TOK_FIN_PROC_ERR: return "FIN_PROC_ERR";
        case TOK_FONCTION: return "FONCTION";
        case TOK_FONCTION_ERR: return "FONCTION_ERR";
        case TOK_FIN_FONCT: return "FIN_FONCT";
        case TOK_FIN_FONCT_ERR: return "FIN_FONCT_ERR";
        case TOK_RETOURNER: return "RETOURNER";
        case TOK_RETOURNER_ERR: return "RETOURNER_ERR";
        
        // 10. Autres tokens spéciaux
        case TOK_EOF: return "EOF";
        case TOK_EOF_ERR: return "EOF_ERR";
        case TOK_COMMENTAIRE: return "COMMENTAIRE";
        case TOK_COMMENTAIRE_ERR: return "COMMENTAIRE_ERR";
        case TOK_COMMENTAIRES: return "COMMENTAIRES";
        case TOK_COMMENTAIRES_ERR: return "COMMENTAIRES_ERR";
        
        default: return "TOKEN_INCONNU";
    }
}



// ============================================================================
// FONCTIONS D'AFFICHAGE
// ============================================================================

// Affiche un token sur une ligne
void afficher_token_ligne(const Token* token) {
    if (token == NULL) {
        printf("Token: NULL\n");
        return;
    }
    
    const char* type_str = token_to_string(token->type);
    
    if (token->valeur != NULL && strlen(token->valeur) > 0) {
        printf("L%03d:C%03d %-25s '%s'\n", 
               token->ligne, token->colonne, type_str, token->valeur);
    } else {
        printf("L%03d:C%03d %-25s\n", 
               token->ligne, token->colonne, type_str);
    }
}

// Affiche un token en format compact
void afficher_token_compact(const Token* token) {
    if (token == NULL) return;
    
    const char* type_str = token_to_string(token->type);
    
    if (est_token_erreur(token->type)) {
        printf("[ERREUR: %s]", type_str);
    } else if (token->valeur != NULL && strlen(token->valeur) > 0) {
        printf("%s", token->valeur);
    } else {
        printf("%s", type_str);
    }
}

// Affiche les informations détaillées d'un token
void afficher_token_detail(const Token* token) {
    if (token == NULL) {
        printf("=== Token NULL ===\n");
        return;
    }
    
    printf("=== Token ===\n");
    printf("Type: %s (%d)\n", token_to_string(token->type), token->type);
    printf("Valeur: '%s'\n", token->valeur);
    printf("Position: Ligne %d, Colonne %d\n", token->ligne, token->colonne);
    
    printf("Propriétés: ");
    if (est_token_erreur(token->type)) printf("[ERREUR] ");
    if (est_mot_cle(token->type)) printf("[MOT-CLE] ");
    if (est_operateur(token->type)) printf("[OPERATEUR] ");
    if (est_separateur(token->type)) printf("[SEPARATEUR] ");
    if (est_constante(token->type)) printf("[CONSTANTE] ");
    if (est_type_donnee(token->type)) printf("[TYPE] ");
    printf("\n");
}

// ============================================================================
// FONCTIONS DE COMPARAISON
// ============================================================================

// Compare deux tokens
bool tokens_egaux(const Token* t1, const Token* t2) {
    if (t1 == t2) return true;
    if (t1 == NULL || t2 == NULL) return false;
    
    return (t1->type == t2->type &&
            strcmp(t1->valeur, t2->valeur) == 0 &&
            t1->ligne == t2->ligne &&
            t1->colonne == t2->colonne);
}

// Compare seulement les types de deux tokens
bool tokens_types_egaux(const Token* t1, const Token* t2) {
    if (t1 == NULL || t2 == NULL) return false;
    return (t1->type == t2->type);
}

// Compare seulement les valeurs de deux tokens
bool tokens_valeurs_egales(const Token* t1, const Token* t2) {
    if (t1 == NULL || t2 == NULL) return false;
    return (strcmp(t1->valeur, t2->valeur) == 0);
}

// ============================================================================
// FONCTIONS DE VALIDATION
// ============================================================================

// Vérifie si un token a une valeur vide
bool token_valeur_vide(const Token* token) {
    if (token == NULL || token->valeur == NULL) return true;
    return (strlen(token->valeur) == 0);
}

// Vérifie si un token est valide (non NULL et type valide)
bool token_valide(const Token* token) {
    if (token == NULL) return false;
    
    // Vérifier que le type est dans la plage valide
    if (token->type < TOK_ALGORITHME || token->type > TOK_COMMENTAIRES_ERR) {
        return false;
    }
    
    // Vérifier la position
    if (token->ligne <= 0 || token->colonne <= 0) {
        return false;
    }
    
    return true;
}