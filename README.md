# Mini Compilateur – Langage Algorithmique

Ce projet consiste à implémenter un mini-compilateur traduisant un langage algorithmique
(en syntaxe française) vers plusieurs langages cibles :
- C
- Java
- Python

Le compilateur suit un pipeline classique :
analyse lexicale → analyse syntaxique → analyse sémantique → génération de code.



## Structure du projet
src/        # Code source du compilateur (.c / .h)
tests/      # Programmes de test (.algo)

## Compilation
Depuis la racine du projet :

```bash
gcc -Wall -Wextra -std=c99 -g -o compilateur \
    src/main.c src/token.c src/lexer.c src/parser.c src/ast.c \
    src/semantique.c src/cgen.c src/jgen.c src/pygen.c
```
## Exécution
```bash
./compilateur tests/valid/test_fonction.algo
```
Le compilateur affiche :
	•	les tokens
	•	les erreurs lexicales / syntaxiques / sémantiques
	•	l’AST
	•	puis génère le code cible selon le choix de l’utilisateur

## Tests

	•	tests/valid/ : programmes corrects
	•	tests/invalid/ : programmes incorrects (erreurs sémantiques / syntaxiques)

## Langages cibles supportés
* Génération de code C (out.c)
* Génération de code Java (Main.java)
* Génération de code Python (out.py)
