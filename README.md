# NSY103 - Mon D.O.S.

Mini système de fichiers pédagogique de **Nael Fares, Nesrine Benhalima et
Jade Beaujour**

La réalisation suit le [rapport du groupe](NSY103-FARES_BENHALIMA_BEAUJOUR.pdf).

## Démarrer sous Linux

Dans Debian :

```sh
sudo apt install build-essential git
git clone https://github.com/NaelFares/NSY103-MiniShell.git
cd NSY103-MiniShell
make
```

Les appels `fork`, `pipe`, `flock` et les files System V nécessitent Linux.

**Terminal 2 : lancer Écran en premier**

```sh
cd ~/NSY103-MiniShell
./bin/ecran
```

**Terminal 1 : créer un nouveau disque et lancer le Shell**

```sh
cd ~/NSY103-MiniShell
./bin/shell --disk disque.dat --format
```

Pour les sessions suivantes, relancer Écran, puis lancer le Shell **sans
`--format`** pour retrouver les données :

```sh
./bin/shell --disk disque.dat
```

`--format` refuse toujours un fichier existant. Le nom du disque peut
contenir un chemin ; son dossier parent doit exister.

## Essayer une session

Saisir dans le Shell ; les résultats apparaissent dans Écran :

```text
help
mkdir cours
cd cours
write note "Bonjour le groupe !\n"
append note "Deuxieme ligne\n"
cat note
ls
pwd
stat note
cp note copie
mv copie /sauvegarde
df
rm note
cd /
rmdir cours
exit
```

Les commandes disponibles sont :

| Commande | Effet |
|---|---|
| `help` | Afficher la syntaxe |
| `pwd`, `cd [chemin]` | Afficher/changer le répertoire courant ; `cd` seul revient à `/` |
| `ls [chemin]` | Lister un répertoire ; `/` après un nom indique un répertoire |
| `mkdir chemin` | Créer un répertoire, parent existant |
| `touch chemin` | Créer un fichier vide ; conserver un fichier existant |
| `write chemin texte` | Créer ou remplacer le contenu d'un fichier |
| `append chemin texte` | Créer ou ajouter à la fin d'un fichier |
| `cat chemin` | Afficher exactement le texte du fichier |
| `cp source destination` | Copier un fichier vers un nouveau nom |
| `mv source destination` | Déplacer/renommer un fichier ou un répertoire vers un nouveau nom |
| `rm chemin` | Supprimer un fichier |
| `rmdir chemin` | Supprimer un répertoire vide hors du chemin courant |
| `stat chemin` | Afficher inode, type, taille, parent et utilisation de l'indirection |
| `df` | Afficher les blocs et inodes utilisés |
| `exit` | Fermer le Noyau, terminer Écran et supprimer la file |

`cp` et `mv` refusent d'écraser une destination ; indiquer son nom complet.
Les commandes acceptent les chemins absolus, relatifs, `.` et `..`.
Les espaces dans les noms ou textes nécessitent des guillemets simples
ou doubles. Les guillemets simples gardent les caractères littéraux ; hors
de ceux-ci, `\n`, `\t` et l'échappement du caractère suivant sont reconnus.
Exemples : `write 'mon fichier' ''` pour un texte vide,
`write note 'un texte avec "guillemets"'`.

Le Shell n'exécute aucune commande du système hôte : pas de `sudo`, de
redirections, de pipelines, de variables ou d'expansion de jokers.
Il traite du texte, pas des fichiers binaires. Une erreur est affichée et
la session peut continuer. En mode script, le code de sortie est 1 si
au moins une commande a échoué, 0 sinon, 2 pour une option invalide.
Une interruption retourne 130. La fin de l'entrée termine aussi la session.

## Organisation conforme au rapport

```text
Terminal 1 : Shell (père)
    | commande : tube anonyme 1
    v
Noyau (fils) -- appels --> bibliothèque SGF -- accès --> disque.dat
    | résultat : tube anonyme 2
    v
Shell -- file de messages System V --> Écran : terminal 2
```

Le Noyau n'accède pas aux structures du disque. Le SGF est compilé en
`build/libsgf.a`. Écran reste un programme indépendant, lancé séparément.
Le Shell ne fait que la saisie, le transport et les diagnostics techniques
dans son terminal. Les résultats des commandes passent tous par Écran.

| Dossier | Responsabilité |
|---|---|
| `include/` | Interfaces communes |
| `src/shell/` | Processus père et saisie |
| `src/noyau/` | Processus fils et coordination |
| `src/sgf/` | Stockage, allocation et commandes du système de fichiers |
| `src/ecran/` | Réception et affichage |
| `src/common/` | Trames des tubes et messages System V |

## Sessions et arrêt

Une seule session par disque et un seul Shell par Écran. La clé de file
par défaut dépend de l'utilisateur Linux. Pour deux sessions indépendantes,
choisir deux disques et deux clés ; la même clé doit être donnée aux deux
programmes d'une session :

```sh
./bin/ecran --key 12345
./bin/shell --key 12345 --disk autre.dat --format
```

`exit` est l'arrêt normal ; Ctrl+C dans le Shell ferme aussi le Noyau et
Écran. Ctrl+C dans Écran supprime la file ; le Shell constatera sa disparition
à la prochaine transmission. Si Écran ne consomme plus les résultats, un
envoi expire après environ cinq secondes au lieu de bloquer indéfiniment.
Après une interruption forcée avec SIGKILL, une file peut rester : vérifier
`ipcs -q`, puis supprimer uniquement la file de cette session avec
`ipcrm -q IDENTIFIANT`. Ne pas supprimer les files d'autres applications.

Un fichier `.lock` vide peut rester après l'arrêt : le verrou est associé
au descripteur ouvert, pas à l'existence du fichier. Ne pas effacer ce fichier
pendant une session. Les disques et fichiers de travail sont exclus de Git.

## Recompiler

```sh
make clean
make
```

Le [guide de la VM](GUIDE-VM.md) décrit la préparation de l’environnement Linux.
