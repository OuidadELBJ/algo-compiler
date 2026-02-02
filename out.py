# Generated Python code

def Factorielle(n):
    if (n <= 1):
        return 1
    else:
        res = (n * Factorielle((n - 1)))
        return res

def Puissance(base, exp):
    acc = 1.0
    for k in range(1, exp + 1):
        acc = (acc * base)
    return acc

def PGCD(a, b):
    while (b != 0):
        tmp = b
        b = (a % b)
        a = tmp
    return a

def EstPair(n):
    return ((n % 2) == 0)

def RemplirDouble(tin, taille):
    for k in range(0, (taille - 1) + 1):
        tin[k] = (k * 2)

def RemplirMatrice(a, n, m):
    for ii in range(0, (n - 1) + 1):
        for jj in range(0, (m - 1) + 1):
            a[ii][jj] = (ii + jj)

def DemoSelon(val):

if __name__ == "__main__":
    msg = "Début du test complet : éàçù ô î – سلام"
    print(msg)
    print("Entrer un entier x :")
    x = input()
    print("Entrer un caractère c :")
    c = input()
    x = ((2 + (3 * 4)) - ((5 + 1) * 2))
    print("x=", x)
    x = -x
    print("x neg=", x)
    ok = (((x >= 0) and not (x != 0)) or False)
    if ok:
        print("OK: logique")
    elif (x < 0):
        print("x est négatif")
    else:
        print("Autre cas")
    r = 0.0
    for i in range(0, (N - 1) + 1):
        if (i == 3):
            break
        r = (r + i)
    print("Somme partielle r=", r)
    i = 0
    while True:
        i = (i + 1)
        if (i == 2):
            break
    print("i après Sortir=", i)
    j = 0
    while True:
        j = (j + 1)
        if (j < 3):
            break
    print("j après Répéter=", j)
    RemplirDouble(t, N)
    print("Tableau t:")
    for i in range(0, (N - 1) + 1):
        print("t[", i, "]=", t[i])
    for i in range(0, (N - 1) + 1):
        tc[i] = c
    print("tc:")
    for i in range(0, (N - 1) + 1):
        print("tc[", i, "]=", tc[i])
    RemplirMatrice(mat, N, M)
    print("Matrice mat:")
    for i in range(0, (N - 1) + 1):
        for j in range(0, (M - 1) + 1):
            print("mat[", i, "][", j, "]=", mat[i][j])
    p.nom = "Ali"
    p.age = 20
    p.estEtudiant = True
    p.adresse.rue = "Rue 1"
    p.adresse.ville = "Casa"
    p.adresse.codePostal = 20000
    print("Personne p: ", p.nom, " ", p.age, " ", p.estEtudiant)
    print("Adresse: ", p.adresse.rue, " ", p.adresse.ville, " ", p.adresse.codePostal)
    for i in range(0, (N - 1) + 1):
        tabP[i].nom = "P"
        tabP[i].age = (18 + i)
        tabP[i].estEtudiant = EstPair(i)
        tabP[i].adresse.ville = "Ville"
        tabP[i].adresse.codePostal = (10000 + i)
    print("=== Étudiants dans tabP ===")
    for i in range(0, (N - 1) + 1):
        if tabP[i].estEtudiant:
            print("-> age=", tabP[i].age, " cp=", tabP[i].adresse.codePostal)
    print("Factorielle(5)=", Factorielle(5))
    print("2^8=", Puissance(2.0, 8))
    print("PGCD(48,18)=", PGCD(48, 18))
    DemoSelon(0)
    DemoSelon(2)
    DemoSelon(9)
