# NSY103 - Mon D.O.S.

## Compiler et lancer

Dans le dossier du projet, sous Linux, compiler avec `make`.

Dans le terminal d'affichage, lancer :

```sh
./bin/ecran
```

Dans un autre terminal ouvert dans le meme dossier, lancer :

```sh
./bin/shell --disk disque.dat --format
```

Les commandes sont saisies dans le Shell et les resultats apparaissent dans Ecran.
Ctrl+D dans le Shell termine les deux programmes. Une seule session a la fois.

Cette premiere etape permet uniquement de saisir `df` pour verifier le parcours des messages. Le SGF est provisoire et ne sauvegarde pas encore de disque.

Les resultats affiches sont limites a 512 octets a cette etape.
