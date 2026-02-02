#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "lexer.h"
#include "token.h"
#include "parser.h"
#include "ast.h"
#include "semantique.h"

#include "cgen.h"
#include "pygen.h"   // à créer
#include "jgen.h"    // à créer

// Lire tout le fichier dans une string (buffer)
static char* lire_fichier_complet(const char* chemin) {
    FILE* f = fopen(chemin, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long taille = ftell(f);
    if (taille < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    char* buf = (char*)malloc((size_t)taille + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t lu = fread(buf, 1, (size_t)taille, f);
    fclose(f);
    buf[lu] = '\0';
    return buf;
}

static void afficher_erreurs_parser(Parser* p) {
    if (!p || p->err_count == 0) {
        printf("Aucune erreur syntaxique.\n");
        return;
    }

    printf("=== Erreurs syntaxiques (%d) ===\n", p->err_count);
    for (int i = 0; i < p->err_count; i++) {
        printf(" %s\n", p->errors[i]);
    }
}

static int demander_cible(void) {
    int choix = 0;

    printf("\n========================================\n");
    printf("Analyse OK \n");
    printf("Vers quel langage veux-tu traduire ?\n");
    printf("  1) C\n");
printf("  2) Java\n");
printf("  3) Python\n");
    printf("Choix: ");
    fflush(stdout);

    if (scanf("%d", &choix) != 1) {
        // Nettoyer stdin si l'utilisateur a tapé n'importe quoi
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {}
        return 0;
    }
    return choix;
}

int main(int argc, char** argv) {
    int code_retour = 0;

    char* source = NULL;
    Lexer* lexer = NULL;
    Parser parser;
    bool parser_inited = false;
    ASTNode* prog = NULL;

    if (argc < 2) {
        printf("Usage: %s <fichier.algo>\n", argv[0]);
        return 1;
    }

    const char* chemin = argv[1];

    // 1) Lire fichier
    source = lire_fichier_complet(chemin);
    if (!source) {
        printf("Impossible de lire le fichier: %s\n", chemin);
        return 1;
    }

    // 2) Lexer
    lexer = creer_lexer(source, chemin);
    if (!lexer) {
        printf("Erreur: creer_lexer() a échoué.\n");
        free(source);
        return 1;
    }

    bool ok_lex = analyser_lexicalement(lexer);

    printf("\n===== TOKENS =====\n");
    afficher_tokens(lexer);

    printf("\n===== ERREURS LEXER =====\n");
    afficher_erreurs(lexer);

    if (!ok_lex) {
        printf("\nAnalyse lexicale échouée.\n");
        code_retour = 2;
        goto cleanup;
    }

    // 3) Récupérer tokens
    int nb_tokens = 0;
    Token* tokens = obtenir_tokens(lexer, &nb_tokens);
    if (!tokens || nb_tokens == 0) {
        printf("Aucun token récupéré.\n");
        code_retour = 2;
        goto cleanup;
    }

    // 4) Parser
    parser_init(&parser, tokens, nb_tokens);
    parser_inited = true;

    prog = parse_program(&parser);

    printf("\n===== ERREURS PARSER =====\n");
    afficher_erreurs_parser(&parser);

    if (parser.err_count > 0 || !prog) {
        printf("\nAnalyse syntaxique échouée.\n");
        code_retour = 3;
        goto cleanup;
    }

    // 5) Afficher AST (si OK)
    printf("\n===== AST (ARBRE SYNTAXIQUE) =====\n");
    ast_print(prog);

    // 6) Sémantique
    {
        SemContext sem;
        sem_init(&sem);

        bool ok_sem = sem_analyze_program(&sem, prog);

        printf("\n===== ERREURS SEMANTIQUE =====\n");
        sem_print_errors(&sem);

        sem_free(&sem);

        if (!ok_sem) {
            printf("\nAnalyse sémantique échouée.\n");
            code_retour = 4;
            goto cleanup;
        }
    }

    printf("\nLexer + Parser + Sémantique OK.\n");

    // 7) Choix de la cible + génération
    {
        int choix = demander_cible();
        bool ok_gen = false;

        switch (choix) {
            case 1: {
                const char* sortie = "out.c";
                ok_gen = cgen_generate(prog, sortie);
                if (ok_gen) printf("Code C généré : %s\n", sortie);
                else printf("Génération C échouée.\n");
                break;
            }

            case 2: {
                const char* sortie = "Main.java";
                ok_gen = jgen_generate(prog, sortie);
                if (ok_gen) printf("Code Java généré : %s\n", sortie);
                else printf("Génération Java échouée.\n");
                break;
            }
            case 3: {
    const char* sortie = "out.py";
    ok_gen = pygen_generate(prog, sortie);
    if (ok_gen) printf("Code Python généré : %s\n", sortie);
    else printf("Génération Python échouée.\n");
    break;
}

            default:
                printf("Choix invalide.\n");
                ok_gen = false;
                break;
        }

        if (!ok_gen) {
            code_retour = 5;
            goto cleanup;
        }
    }

    code_retour = 0;

cleanup:
    if (prog) ast_free(prog);
    if (parser_inited) parser_free(&parser);
    if (lexer) detruire_lexer(lexer);
    free(source);

    return code_retour;
}
