#include "lexer.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// PROTOTYPES DES FONCTIONS STATIQUES

static bool est_fin_source(Lexer* lexer);
static char caractere_courant(Lexer* lexer);
static char caractere_suivant(Lexer* lexer, int offset);
static char caractere_precedent(Lexer* lexer);
static void avancer(Lexer* lexer, int n);

static bool est_blanc(char c);
static bool est_chiffre(char c);
static bool est_lettre(char c);

static void ajouter_token(Lexer* lexer, TokenType type, const char* valeur);
static void ajouter_message_erreur(Lexer* lexer, const char* message);
static void ajouter_erreur_lexicale(Lexer* lexer, TokenType type_erreur,
                                   const char* valeur, const char* message);

static void ignorer_espaces(Lexer* lexer);
static void ignorer_espaces_sans_nl(Lexer* lexer);

static TokenType trouver_mot_cle(const char* mot, TokenType* type_erreur);

static void lire_identifiant(Lexer* lexer);
static void lire_nombre(Lexer* lexer);
static void lire_nombre_commence_par_point(Lexer* lexer);

static void lire_chaine(Lexer* lexer);
static void lire_commentaire_ligne(Lexer* lexer);
static void lire_commentaire_bloc(Lexer* lexer);
static void traiter_operateurs(Lexer* lexer);

static bool doit_generer_fin_instr(Lexer* lexer);

// FONCTIONS STATIQUES AUXILIAIRES

static bool est_fin_source(Lexer* lexer) {
    return lexer->position >= (int)strlen(lexer->source);
}

static char caractere_courant(Lexer* lexer) {
    if (est_fin_source(lexer)) return '\0';
    return lexer->source[lexer->position];
}

static char caractere_suivant(Lexer* lexer, int offset) {
    int pos = lexer->position + offset;
    if (pos >= (int)strlen(lexer->source)) return '\0';
    return lexer->source[pos];
}

static char caractere_precedent(Lexer* lexer) {
    int pos = lexer->position - 1;
    if (pos < 0) return '\0';
    return lexer->source[pos];
}

static void avancer(Lexer* lexer, int n) {
    for (int i = 0; i < n; i++) {
        if (caractere_courant(lexer) == '\n') {
            lexer->ligne++;
            lexer->colonne = 1;
        } else {
            lexer->colonne++;
        }
        lexer->position++;
    }
}

static bool est_blanc(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool est_chiffre(char c) {
    return c >= '0' && c <= '9';
}

static bool est_lettre(char c) {
    unsigned char uc = (unsigned char)c;

    if ((uc >= 'a' && uc <= 'z') ||
        (uc >= 'A' && uc <= 'Z') ||
        uc == '_') {
        return true;
    }

    // UTF-8 : on accepte tout octet >= 128 comme "lettre"
    if (uc & 0x80) {
        return true;
    }

    return false;
}

// MOTS-CLES

typedef struct {
    const char* mot;
    TokenType type_normal;
    TokenType type_erreur;
} MotCle;

static const MotCle MOTS_CLES[] = {
    // 1. Structure
    {"Algorithme", TOK_ALGORITHME, TOK_ALGORITHME_ERR},
    {"algorithme", TOK_ALGORITHME, TOK_ALGORITHME_ERR},
    {"Début", TOK_DEBUT, TOK_DEBUT_ERR},
    {"debut", TOK_DEBUT, TOK_DEBUT_ERR},
    {"Fin", TOK_FIN, TOK_FIN_ERR},
    {"fin", TOK_FIN, TOK_FIN_ERR},

    // 2. Déclarations / types
    {"Objets", TOK_OBJETS, TOK_OBJETS_ERR},
    {"objets", TOK_OBJETS, TOK_OBJETS_ERR},
    {"Variable", TOK_VARIABLE, TOK_VARIABLE_ERR},
    {"variable", TOK_VARIABLE, TOK_VARIABLE_ERR},
    {"Constante", TOK_CONSTANTE, TOK_CONSTANTE_ERR},
    {"constante", TOK_CONSTANTE, TOK_CONSTANTE_ERR},

    {"entier", TOK_ENTIER, TOK_ENTIER_ERR},
    {"réel", TOK_REEL, TOK_REEL_ERR},
    {"reel", TOK_REEL, TOK_REEL_ERR},
    {"caractère", TOK_CARACTERE, TOK_CARACTERE_ERR},
    {"caractere", TOK_CARACTERE, TOK_CARACTERE_ERR},
    {"chaine", TOK_CHAINE, TOK_CHAINE_ERR},
    {"chaîne", TOK_CHAINE, TOK_CHAINE_ERR},
    {"booléen", TOK_BOOLEEN, TOK_BOOLEEN_ERR},
    {"booleen", TOK_BOOLEEN, TOK_BOOLEEN_ERR},
    {"tableau", TOK_TABLEAU, TOK_TABLEAU_ERR},
    {"Tableau", TOK_TABLEAU, TOK_TABLEAU_ERR},

    {"de", TOK_DE, TOK_DE_ERR},

    {"Structure", TOK_STRUCTURE, TOK_STRUCTURE_ERR},
    {"structure", TOK_STRUCTURE, TOK_STRUCTURE_ERR},
    {"Fin-struct", TOK_FIN_STRUCT, TOK_FIN_STRUCT_ERR},
    {"fin-struct", TOK_FIN_STRUCT, TOK_FIN_STRUCT_ERR},
    {"finstruct", TOK_FIN_STRUCT, TOK_FIN_STRUCT_ERR},

    // 3. IO
    {"Ecrire", TOK_ECRIRE, TOK_ECRIRE_ERR},
    {"ecrire", TOK_ECRIRE, TOK_ECRIRE_ERR},
    {"Lire", TOK_LIRE, TOK_LIRE_ERR},
    {"lire", TOK_LIRE, TOK_LIRE_ERR},
    {"Retour", TOK_RETOUR, TOK_RETOUR_ERR},
    {"retour", TOK_RETOUR, TOK_RETOUR_ERR},

    // 4. Logique
    {"Vrai", TOK_VRAI, TOK_VRAI_ERR},
    {"vrai", TOK_VRAI, TOK_VRAI_ERR},
    {"Faux", TOK_FAUX, TOK_FAUX_ERR},
    {"faux", TOK_FAUX, TOK_FAUX_ERR},
    {"Et", TOK_ET, TOK_ET_ERR},
    {"et", TOK_ET, TOK_ET_ERR},
    {"Ou", TOK_OU, TOK_OU_ERR},
    {"ou", TOK_OU, TOK_OU_ERR},
    {"Non", TOK_NON, TOK_NON_ERR},
    {"non", TOK_NON, TOK_NON_ERR},

    // 7. Op arithm mots
    {"Div", TOK_DIV_ENTIER, TOK_DIV_ENTIER_ERR},
    {"div", TOK_DIV_ENTIER, TOK_DIV_ENTIER_ERR},
    {"Mod", TOK_MODULO, TOK_MODULO_ERR},
    {"mod", TOK_MODULO, TOK_MODULO_ERR},

    // 8. Contrôle
    {"Si", TOK_SI, TOK_SI_ERR},
    {"si", TOK_SI, TOK_SI_ERR},
    {"SinonSi", TOK_SINONSI, TOK_SINONSI_ERR},
    {"sinonsi", TOK_SINONSI, TOK_SINONSI_ERR},
    {"sinon-si", TOK_SINONSI, TOK_SINONSI_ERR},
    {"Sinon", TOK_SINON, TOK_SINON_ERR},
    {"sinon", TOK_SINON, TOK_SINON_ERR},
    {"Alors", TOK_ALORS, TOK_ALORS_ERR},
    {"alors", TOK_ALORS, TOK_ALORS_ERR},
    {"FinSi", TOK_FIN_SI, TOK_FIN_SI_ERR},
    {"finsi", TOK_FIN_SI, TOK_FIN_SI_ERR},

    {"Selon", TOK_SELON, TOK_SELON_ERR},
    {"selon", TOK_SELON, TOK_SELON_ERR},
    {"Cas", TOK_CAS, TOK_CAS_ERR},
    {"cas", TOK_CAS, TOK_CAS_ERR},

    {"Défaut", TOK_DEFAUT, TOK_DEFAUT_ERR},
    {"défaut", TOK_DEFAUT, TOK_DEFAUT_ERR},
    {"defaut", TOK_DEFAUT, TOK_DEFAUT_ERR},
    {"Defaut", TOK_DEFAUT, TOK_DEFAUT_ERR},

    {"FinSelon", TOK_FIN_SELON, TOK_FIN_SELON_ERR},
    {"finselon", TOK_FIN_SELON, TOK_FIN_SELON_ERR},

    {"Sortir", TOK_SORTIR, TOK_SORTIR_ERR},
    {"sortir", TOK_SORTIR, TOK_SORTIR_ERR},

    {"Pour", TOK_POUR, TOK_POUR_ERR},
    {"pour", TOK_POUR, TOK_POUR_ERR},

    {"jusqu'à", TOK_JUSQUA, TOK_JUSQUA_ERR},
    {"jusqua", TOK_JUSQUA, TOK_JUSQUA_ERR},
    {"Jusqua", TOK_JUSQUA, TOK_JUSQUA_ERR},
    {"JusquA", TOK_JUSQUA, TOK_JUSQUA_ERR},
    {"JUSQUA", TOK_JUSQUA, TOK_JUSQUA_ERR},

    {"Répéter", TOK_REPETER, TOK_REPETER_ERR},
    {"repeter", TOK_REPETER, TOK_REPETER_ERR},
    {"répéter", TOK_REPETER, TOK_REPETER_ERR},

    {"pas", TOK_PAS, TOK_PAS_ERR},

    {"FinPour", TOK_FIN_POUR, TOK_FIN_POUR_ERR},
    {"finpour", TOK_FIN_POUR, TOK_FIN_POUR_ERR},

    {"Quitter", TOK_QUITTER_POUR, TOK_QUITTER_POUR_ERR},
    {"quitter", TOK_QUITTER_POUR, TOK_QUITTER_POUR_ERR},

    {"TantQue", TOK_TANTQUE, TOK_TANTQUE_ERR},
    {"tantque", TOK_TANTQUE, TOK_TANTQUE_ERR},
    {"FinTantQue", TOK_FINTANTQUE, TOK_FINTANTQUE_ERR},
    {"fintantque", TOK_FINTANTQUE, TOK_FINTANTQUE_ERR},

    // 9. Proc / fct
    {"Procédure", TOK_PROCEDURE, TOK_PROCEDURE_ERR},
    {"procedure", TOK_PROCEDURE, TOK_PROCEDURE_ERR},
    {"FinProc", TOK_FIN_PROC, TOK_FIN_PROC_ERR},
    {"finproc", TOK_FIN_PROC, TOK_FIN_PROC_ERR},
    {"Fonction", TOK_FONCTION, TOK_FONCTION_ERR},
    {"fonction", TOK_FONCTION, TOK_FONCTION_ERR},
    {"FinFonct", TOK_FIN_FONCT, TOK_FIN_FONCT_ERR},
    {"finfonct", TOK_FIN_FONCT, TOK_FIN_FONCT_ERR},
    {"Retourner", TOK_RETOURNER, TOK_RETOURNER_ERR},
    {"retourner", TOK_RETOURNER, TOK_RETOURNER_ERR},

    {NULL, TOK_ID, TOK_ID_ERR}
};

// AJOUT TOKEN / ERREUR

static void ajouter_token(Lexer* lexer, TokenType type, const char* valeur) {
    if (lexer->nb_tokens >= lexer->capacite_tokens) {
        lexer->capacite_tokens *= 2;
        Token* tmp = realloc(lexer->tokens, lexer->capacite_tokens * sizeof(Token));
        if (!tmp) return;
        lexer->tokens = tmp;
    }

    Token* token = &lexer->tokens[lexer->nb_tokens++];
    token->type = type;
    token->valeur = strdup(valeur ? valeur : "");
    token->ligne = lexer->ligne;
    token->colonne = lexer->colonne - (int)strlen(valeur ? valeur : "");
    if (token->colonne < 1) token->colonne = 1;
}

static void ajouter_message_erreur(Lexer* lexer, const char* message) {
    if (lexer->nb_erreurs >= lexer->capacite_erreurs) {
        lexer->capacite_erreurs *= 2;
        char** tmp = realloc(lexer->messages_erreur, lexer->capacite_erreurs * sizeof(char*));
        if (!tmp) return;
        lexer->messages_erreur = tmp;
    }

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s:%d:%d: %s",
             lexer->nom_fichier, lexer->ligne, lexer->colonne, message);

    lexer->messages_erreur[lexer->nb_erreurs++] = strdup(buffer);
}

static void ajouter_erreur_lexicale(Lexer* lexer, TokenType type_erreur,
                                   const char* valeur, const char* message) {
    ajouter_token(lexer, type_erreur, valeur ? valeur : "");
    ajouter_message_erreur(lexer, message ? message : "Erreur lexicale");
    (void)lexer;
}

// ESPACES / FIN INSTRUCTION

static bool doit_generer_fin_instr(Lexer* lexer) {
    if (lexer->nb_tokens == 0) return false;

    // Pas de FIN_INSTR à l'intérieur de () ou []
    if (lexer->paren_depth > 0 || lexer->bracket_depth > 0) return false;

    Token* dernier = &lexer->tokens[lexer->nb_tokens - 1];
    if (dernier->type == TOK_FIN_INSTR) return false;

    return true;
}

static void ignorer_espaces(Lexer* lexer) {
    while (!est_fin_source(lexer) && est_blanc(caractere_courant(lexer))) {
        if (caractere_courant(lexer) == '\n') {
            if (doit_generer_fin_instr(lexer)) {
                ajouter_token(lexer, TOK_FIN_INSTR, "");
            }
        }
        avancer(lexer, 1);
    }
}

// Utile pour "Quitter Pour" : on ne saute pas les \n
static void ignorer_espaces_sans_nl(Lexer* lexer) {
    while (!est_fin_source(lexer)) {
        char c = caractere_courant(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            avancer(lexer, 1);
        } else {
            break;
        }
    }
}

// MOTS-CLES

static TokenType trouver_mot_cle(const char* mot, TokenType* type_erreur) {
    for (int i = 0; MOTS_CLES[i].mot != NULL; i++) {
        if (strcmp(MOTS_CLES[i].mot, mot) == 0) {
            if (type_erreur) *type_erreur = MOTS_CLES[i].type_erreur;
            return MOTS_CLES[i].type_normal;
        }
    }
    return TOK_ID;
}

// LECTURE IDENTIFIANT

static void lire_identifiant(Lexer* lexer) {
    int start_pos = lexer->position;
    int start_col = lexer->colonne;

    while (!est_fin_source(lexer) &&
           (est_lettre(caractere_courant(lexer)) ||
            est_chiffre(caractere_courant(lexer)) ||
            caractere_courant(lexer) == '_' ||
            caractere_courant(lexer) == '\'' ||
            caractere_courant(lexer) == '-')) {
        avancer(lexer, 1);
    }

    int length = lexer->position - start_pos;
    char* lexeme = (char*)malloc(length + 1);
    strncpy(lexeme, &lexer->source[start_pos], length);
    lexeme[length] = '\0';

    TokenType type_erreur;
    TokenType type = trouver_mot_cle(lexeme, &type_erreur);

    // Traitement spécial "Quitter Pour"
    if (type == TOK_QUITTER_POUR) {
        ignorer_espaces_sans_nl(lexer);

        int sauvegarde_pos = lexer->position;
        int sauvegarde_ligne = lexer->ligne;
        int sauvegarde_col = lexer->colonne;

        // Lire le mot suivant
        int wstart = lexer->position;
        while (!est_fin_source(lexer) &&
               (est_lettre(caractere_courant(lexer)) || caractere_courant(lexer) == '\'' || caractere_courant(lexer) == '-')) {
            avancer(lexer, 1);
        }
        int wlen = lexer->position - wstart;

        char* suivant = (char*)malloc(wlen + 1);
        strncpy(suivant, &lexer->source[wstart], wlen);
        suivant[wlen] = '\0';

        if (strcmp(suivant, "Pour") == 0 || strcmp(suivant, "pour") == 0) {
            char* combi = (char*)malloc(strlen(lexeme) + strlen(suivant) + 2);
            sprintf(combi, "%s %s", lexeme, suivant);
            ajouter_token(lexer, TOK_QUITTER_POUR, combi);
            free(combi);
            free(suivant);
            free(lexeme);
            return;
        }

        // Sinon : retour en arrière (on garde juste "Quitter")
        lexer->position = sauvegarde_pos;
        lexer->ligne = sauvegarde_ligne;
        lexer->colonne = sauvegarde_col;

        free(suivant);
        // Ici on laisse "Quitter" comme TOK_QUITTER_POUR (design actuel).
    }

    if (type != TOK_ID) {
        ajouter_token(lexer, type, lexeme);
    } else {
        ajouter_token(lexer, TOK_ID, lexeme);
    }

    free(lexeme);

    (void)start_col;
}

// LECTURE NOMBRE : 1,5 ET 1.5

static void lire_nombre(Lexer* lexer) {
    int start_pos = lexer->position;
    bool est_reel = false;
    bool erreur = false;

    // Partie entière
    while (!est_fin_source(lexer) && est_chiffre(caractere_courant(lexer))) {
        avancer(lexer, 1);
    }

    // Décimal: ',' ou '.' uniquement si suivi d'un chiffre
    char c = caractere_courant(lexer);
    char n = caractere_suivant(lexer, 1);

    if ((c == ',' || c == '.') && est_chiffre(n)) {
        est_reel = true;
        avancer(lexer, 1);

        // Partie fractionnaire
        while (!est_fin_source(lexer) && est_chiffre(caractere_courant(lexer))) {
            avancer(lexer, 1);
        }
    }

    int length = lexer->position - start_pos;
    char* nombre = (char*)malloc((size_t)length + 1);
    strncpy(nombre, &lexer->source[start_pos], (size_t)length);
    nombre[length] = '\0';

    if (length == 0) erreur = true;

    if (erreur) {
        if (est_reel) ajouter_erreur_lexicale(lexer, TOK_CONST_REEL_ERR, nombre, "Constante réelle invalide");
        else ajouter_erreur_lexicale(lexer, TOK_CONST_ENTIERE_ERR, nombre, "Constante entière invalide");
    } else {
        if (est_reel) ajouter_token(lexer, TOK_CONST_REEL, nombre);
        else ajouter_token(lexer, TOK_CONST_ENTIERE, nombre);
    }

    free(nombre);
}

// Lecture d'un réel du type ".5"
static void lire_nombre_commence_par_point(Lexer* lexer) {
    int start_pos = lexer->position; // sur '.'
    avancer(lexer, 1);

    while (!est_fin_source(lexer) && est_chiffre(caractere_courant(lexer))) {
        avancer(lexer, 1);
    }

    int length = lexer->position - start_pos;
    char* nombre = (char*)malloc(length + 1);
    strncpy(nombre, &lexer->source[start_pos], length);
    nombre[length] = '\0';

    ajouter_token(lexer, TOK_CONST_REEL, nombre);
    free(nombre);
}

// CHAÎNES / COMMENTAIRES

static void lire_chaine(Lexer* lexer) {
    char delimiteur = caractere_courant(lexer);
    avancer(lexer, 1);

    int start_pos = lexer->position;
    bool escape = false;

    while (!est_fin_source(lexer)) {
        if (escape) {
            escape = false;
            avancer(lexer, 1);
            continue;
        }

        if (caractere_courant(lexer) == '\\') {
            escape = true;
            avancer(lexer, 1);
            continue;
        }

        if (caractere_courant(lexer) == delimiteur) {
            break;
        }

        if (caractere_courant(lexer) == '\n') {
            break;
        }

        avancer(lexer, 1);
    }

    int length = lexer->position - start_pos;
    char* contenu = (char*)malloc(length + 1);
    strncpy(contenu, &lexer->source[start_pos], length);
    contenu[length] = '\0';

    if (est_fin_source(lexer) || caractere_courant(lexer) != delimiteur) {
        ajouter_erreur_lexicale(lexer, TOK_CONST_CHAINE_ERR, contenu, "Chaîne non fermée");
        free(contenu);
        return;
    }

    ajouter_token(lexer, TOK_CONST_CHAINE, contenu);
    free(contenu);

    avancer(lexer, 1);
}

static void lire_commentaire_ligne(Lexer* lexer) {
    avancer(lexer, 2); // "//"
    int start_pos = lexer->position;

    while (!est_fin_source(lexer) && caractere_courant(lexer) != '\n') {
        avancer(lexer, 1);
    }

    int length = lexer->position - start_pos;
    char* commentaire = (char*)malloc(length + 1);
    strncpy(commentaire, &lexer->source[start_pos], length);
    commentaire[length] = '\0';
    ajouter_token(lexer, TOK_COMMENTAIRE, commentaire);
    free(commentaire);
}

static void lire_commentaire_bloc(Lexer* lexer) {
    avancer(lexer, 2); // "/*"
    int start_pos = lexer->position;

    while (!est_fin_source(lexer)) {
        if (caractere_courant(lexer) == '*' && caractere_suivant(lexer, 1) == '/') {
            break;
        }
        avancer(lexer, 1);
    }

    if (est_fin_source(lexer)) {
        ajouter_erreur_lexicale(lexer, TOK_COMMENTAIRES_ERR, "", "Commentaire bloc non fermé");
        return;
    }

    int length = lexer->position - start_pos;
    char* commentaire = (char*)malloc(length + 1);
    strncpy(commentaire, &lexer->source[start_pos], length);
    commentaire[length] = '\0';
    ajouter_token(lexer, TOK_COMMENTAIRES, commentaire);
    free(commentaire);

    avancer(lexer, 2); // "*/"
}

// OPÉRATEURS / SYMBOLES

static void traiter_operateurs(Lexer* lexer) {
    char courant = caractere_courant(lexer);
    char suivant = caractere_suivant(lexer, 1);

    // Chaînes
    if (courant == '"' || courant == '\'') {
        lire_chaine(lexer);
        return;
    }

    switch (courant) {
        case '<':
            if (suivant == '-') {
                avancer(lexer, 2);
                ajouter_token(lexer, TOK_AFFECTATION, "<-");
            } else if (suivant == '=') {
                avancer(lexer, 2);
                ajouter_token(lexer, TOK_INFERIEUR_EGAL, "<=");
            } else if (suivant == '>') {
                avancer(lexer, 2);
                ajouter_token(lexer, TOK_DIFFERENT, "<>");
            } else {
                avancer(lexer, 1);
                ajouter_token(lexer, TOK_INFERIEUR, "<");
            }
            break;

        case '>':
            if (suivant == '=') {
                avancer(lexer, 2);
                ajouter_token(lexer, TOK_SUPERIEUR_EGAL, ">=");
            } else {
                avancer(lexer, 1);
                ajouter_token(lexer, TOK_SUPERIEUR, ">");
            }
            break;

        case '=':
            avancer(lexer, 1);
            ajouter_token(lexer, TOK_EGAL, "=");
            break;

        case '+':
            avancer(lexer, 1);
            ajouter_token(lexer, TOK_PLUS, "+");
            break;

        case '-':
            avancer(lexer, 1);
            ajouter_token(lexer, TOK_MOINS, "-");
            break;

        case '*':
            avancer(lexer, 1);
            ajouter_token(lexer, TOK_FOIS, "*");
            break;

        case '/':
            if (suivant == '/') {
                lire_commentaire_ligne(lexer);
            } else if (suivant == '*') {
                lire_commentaire_bloc(lexer);
            } else {
                avancer(lexer, 1);
                ajouter_token(lexer, TOK_DIVISE, "/");
            }
            break;

        case '^':
            avancer(lexer, 1);
            ajouter_token(lexer, TOK_PUISSANCE, "^");
            break;

        case ':':
            avancer(lexer, 1);
            ajouter_token(lexer, TOK_DEUX_POINTS, ":");
            break;

        case ',':
            avancer(lexer, 1);
            ajouter_token(lexer, TOK_VIRGULE, ",");
            break;

        case '(':
            avancer(lexer, 1);
            lexer->paren_depth++;
            ajouter_token(lexer, TOK_PAREN_OUVRANTE, "(");
            break;

        case ')':
            avancer(lexer, 1);
            if (lexer->paren_depth > 0) lexer->paren_depth--;
            ajouter_token(lexer, TOK_PAREN_FERMANTE, ")");
            break;

        case '[':
            avancer(lexer, 1);
            lexer->bracket_depth++;
            ajouter_token(lexer, TOK_CROCHET_OUVRANT, "[");
            break;

        case ']':
            avancer(lexer, 1);
            if (lexer->bracket_depth > 0) lexer->bracket_depth--;
            ajouter_token(lexer, TOK_CROCHET_FERMANT, "]");
            break;

        case '.':
            // Gestion du cas ".5" (réel)
            if (est_chiffre(suivant) && !est_chiffre(caractere_precedent(lexer))) {
                lire_nombre_commence_par_point(lexer);
            } else {
                avancer(lexer, 1);
                ajouter_token(lexer, TOK_POINT, ".");
            }
            break;

        default: {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     "Caractère inconnu: '%c' (0x%02x)",
                     courant, (unsigned char)courant);

            char tmp[2] = {courant, '\0'};
            ajouter_erreur_lexicale(lexer, TOK_ID_ERR, tmp, msg);
            avancer(lexer, 1);
            break;
        }
    }
}

// API PUBLIQUE

Lexer* creer_lexer(const char* source, const char* nom_fichier) {
    Lexer* lexer = (Lexer*)malloc(sizeof(Lexer));
    if (!lexer) return NULL;

    lexer->source = source;
    lexer->position = 0;
    lexer->ligne = 1;
    lexer->colonne = 1;

    lexer->nb_tokens = 0;
    lexer->capacite_tokens = 256;
    lexer->tokens = (Token*)malloc(lexer->capacite_tokens * sizeof(Token));

    lexer->nb_erreurs = 0;
    lexer->capacite_erreurs = 16;
    lexer->messages_erreur = (char**)malloc(lexer->capacite_erreurs * sizeof(char*));

    lexer->nom_fichier = strdup(nom_fichier ? nom_fichier : "stdin");
    lexer->mode_strict = false;

    lexer->paren_depth = 0;
    lexer->bracket_depth = 0;

    return lexer;
}

void detruire_lexer(Lexer* lexer) {
    if (!lexer) return;

    for (int i = 0; i < lexer->nb_tokens; i++) {
        free(lexer->tokens[i].valeur);
    }
    free(lexer->tokens);

    for (int i = 0; i < lexer->nb_erreurs; i++) {
        free(lexer->messages_erreur[i]);
    }
    free(lexer->messages_erreur);

    free(lexer->nom_fichier);
    free(lexer);
}

bool analyser_lexicalement(Lexer* lexer) {
    if (!lexer || !lexer->source) return false;

    while (!est_fin_source(lexer)) {
        char courant = caractere_courant(lexer);

        if (est_blanc(courant)) {
            ignorer_espaces(lexer);
            continue;
        }

        if (est_chiffre(courant)) {
            lire_nombre(lexer);
            continue;
        }

        if (est_lettre(courant)) {
            lire_identifiant(lexer);
            continue;
        }

        traiter_operateurs(lexer);
    }

    ajouter_token(lexer, TOK_EOF, "");
    return lexer->nb_erreurs == 0;
}

Token* obtenir_tokens(Lexer* lexer, int* nb_tokens) {
    if (nb_tokens) *nb_tokens = lexer->nb_tokens;
    return lexer->tokens;
}

char** obtenir_messages_erreur(Lexer* lexer, int* nb_erreurs) {
    if (nb_erreurs) *nb_erreurs = lexer->nb_erreurs;
    return lexer->messages_erreur;
}

void afficher_token(Token* token) {
    if (!token) return;

    printf("L%03d:C%03d %-20s '%s'\n",
           token->ligne,
           token->colonne,
           token_to_string(token->type),
           token->valeur);
}

int compter_tokens_erreur(Lexer* lexer) {
    int count = 0;
    for (int i = 0; i < lexer->nb_tokens; i++) {
        if (lexer->tokens[i].type % 2 == 1) {
            count++;
        }
    }
    return count;
}

void afficher_tokens(Lexer* lexer) {
    if (!lexer) {
        printf("Lexer NULL : aucun token à afficher.\n");
        return;
    }

    if (lexer->nb_tokens == 0) {
        printf("=== Aucun token généré ===\n");
        return;
    }

    printf("=== Tokens générés (%d) ===\n", lexer->nb_tokens);
    for (int i = 0; i < lexer->nb_tokens; i++) {
        printf("%4d: ", i);
        afficher_token(&lexer->tokens[i]);
    }

    int nb_err = compter_tokens_erreur(lexer);
    if (nb_err > 0) {
        printf("\n⚠ %d token(s) d'erreur détecté(s) dans le flux de tokens.\n", nb_err);
    } else {
        printf("\nAucun token d'erreur dans le flux.\n");
    }
}

void afficher_erreurs(Lexer* lexer) {
    if (lexer->nb_erreurs == 0) {
        printf("Aucune erreur lexicale détectée.\n");
        return;
    }

    printf("=== Erreurs lexicales (%d) ===\n", lexer->nb_erreurs);
    for (int i = 0; i < lexer->nb_erreurs; i++) {
        printf("%s\n", lexer->messages_erreur[i]);
    }
}

void set_mode_strict(Lexer* lexer, bool strict) {
    if (lexer) {
        lexer->mode_strict = strict;
    }
}
