#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

// Structures
typedef struct Adresse {
    char* rue;
    char* ville;
    int codePostal;
} Adresse;

typedef struct Personne {
    char* nom;
    int age;
    bool estEtudiant;
    Adresse adresse;
} Personne;

// Constantes
enum {
    N = 5,
    M = 3,
};

// Globales
int i;
int j;
int x;
int y;
double r;
bool ok;
char c;
char* msg = NULL;
int t[N];
int mat[N][M];
char tc[N];
Personne p;
Personne tabP[N];

// Fonctions
int Factorielle(int n) {
    int res;
    if ((n <= 1)) {
        return 1;
    }
    else {
        res = (n * Factorielle((n - 1)));
        return res;
    }
}

double Puissance(double base, int exp) {
    int k;
    double acc;
    acc = 1.0;
    for (k = 1; k <= exp; k++) {
        acc = (acc * base);
    }
    return acc;
}

int PGCD(int a, int b) {
    int tmp;
    while ((b != 0)) {
        tmp = b;
        b = (a % b);
        a = tmp;
    }
    return a;
}

bool EstPair(int n) {
    return ((n % 2) == 0);
}

void RemplirDouble(int tin[], int taille) {
    int k;
    for (k = 0; k <= (taille - 1); k++) {
        tin[k] = (k * 2);
    }
}

void RemplirMatrice(int a[], int n, int m) {
    int ii;
    int jj;
    for (ii = 0; ii <= (n - 1); ii++) {
        for (jj = 0; jj <= (m - 1); jj++) {
            a[(ii) * m + (jj)] = (ii + jj);
        }
    }
}

void DemoSelon(int val) {
    switch (val) {
    case 0:
{
        printf("val=0\n");
    }
    break;
    case 1:
    case 2:
{
        printf("val=1 ou 2\n");
    }
    break;
    case 3:
{
        printf("val=3\n");
    }
    break;
    default:
{
        printf("val autre\n");
    }
    }
}

// Main
int main(void) {
    msg = "Début du test complet : éàçù ô î – سلام";
    printf("%s\n", msg);
    printf("Entrer un entier x :\n");
    scanf("%d", &x);
    printf("Entrer un caractère c :\n");
    scanf(" %c", &c);
    x = ((2 + (3 * 4)) - ((5 + 1) * 2));
    printf("x=%d\n", x);
    x = -(x);
    printf("x neg=%d\n", x);
    ok = (((x >= 0) && !((x != 0))) || false);
    if (ok) {
        printf("OK: logique\n");
    }
    else {
        printf("Autre cas\n");
    }
    r = 0.0;
    for (i = 0; i <= (N - 1); i++) {
        if ((i == 3)) {
            break;
        }
        r = (r + i);
    }
    printf("Somme partielle r=%g\n", r);
    i = 0;
    while (true) {
        i = (i + 1);
        if ((i == 2)) {
            break;
        }
    }
    printf("i après Sortir=%d\n", i);
    j = 0;
    do {
        j = (j + 1);
    }
    while ((j < 3));
    printf("j après Répéter=%d\n", j);
    RemplirDouble(t, N);
    printf("Tableau t:\n");
    for (i = 0; i <= (N - 1); i++) {
        printf("t[%d]=%d\n", i, t[i]);
    }
    for (i = 0; i <= (N - 1); i++) {
        tc[i] = c;
    }
    printf("tc:\n");
    for (i = 0; i <= (N - 1); i++) {
        printf("tc[%d]=%c\n", i, tc[i]);
    }
    RemplirMatrice((int*)mat, N, M);
    printf("Matrice mat:\n");
    for (i = 0; i <= (N - 1); i++) {
        for (j = 0; j <= (M - 1); j++) {
            printf("mat[%d][%d]=%d\n", i, j, mat[i][j]);
        }
    }
    p.nom = "Ali";
    p.age = 20;
    p.estEtudiant = true;
    p.adresse.rue = "Rue 1";
    p.adresse.ville = "Casa";
    p.adresse.codePostal = 20000;
    printf("Personne p: %s %d %d\n", p.nom, p.age, p.estEtudiant);
    printf("Adresse: %s %s %d\n", p.adresse.rue, p.adresse.ville, p.adresse.codePostal);
    for (i = 0; i <= (N - 1); i++) {
        tabP[i].nom = "P";
        tabP[i].age = (18 + i);
        tabP[i].estEtudiant = EstPair(i);
        tabP[i].adresse.ville = "Ville";
        tabP[i].adresse.codePostal = (10000 + i);
    }
    printf("=== Étudiants dans tabP ===\n");
    for (i = 0; i <= (N - 1); i++) {
        if (tabP[i].estEtudiant) {
            printf("-> age=%d cp=%d\n", tabP[i].age, tabP[i].adresse.codePostal);
        }
    }
    printf("Factorielle(5)=%d\n", Factorielle(5));
    printf("2^8=%g\n", Puissance(2.0, 8));
    printf("PGCD(48,18)=%d\n", PGCD(48, 18));
    DemoSelon(0);
    DemoSelon(2);
    DemoSelon(9);
    return 0;
}
