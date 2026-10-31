# NSY103 - Mon D.O.S.

Projet de **Nael Fares, Nesrine Benhalima et Jade Beaujour**.

Mon D.O.S. permet de créer et de manipuler des fichiers et des répertoires sur un disque virtuel enregistré dans un fichier `.dat`.
Les commandes sont saisies dans un terminal et les résultats sont affichés dans un second terminal.

## Organisation du projet

| Dossier | Contenu |
|---|---|
| `shell/` | Saisie des commandes et lancement du Noyau |
| `noyau/` | Réception des commandes et appel des fonctions du SGF |
| `sgf/` | Gestion des fichiers, des répertoires et des blocs du disque |
| `ecran/` | Réception et affichage des résultats |
| `commun/` | Échanges par tubes et par file de messages |

Dans `sgf/`, `commands.c` interprète les commandes, `functions.c` contient les
primitives de fichiers et de répertoires, et `storage.c` gère les inodes et les blocs.

Le Shell crée le Noyau comme processus fils.
Deux tubes anonymes servent à transmettre les commandes et les réponses.
Le Noyau utilise la bibliothèque SGF pour accéder au disque.
Le Shell transmet ensuite les résultats à Écran par une file de messages System V.

## Compilation

Le projet nécessite Linux, un compilateur C compatible C11, Make et l'outil `ar`.
Installer ces outils avec le gestionnaire de paquets de votre distribution, puis ouvrir un terminal dans le dossier du projet :

```sh
make
```

Le Makefile crée les programmes `bin/shell` et `bin/ecran`, ainsi que la
bibliothèque `build/libsgf.a`. Les dossiers `bin/` et `build/` sont créés
automatiquement.

## Lancement

Ouvrir deux terminaux avec le même compte Linux, dans le dossier du projet.
Cette V1 prévoit une seule session à la fois et une file de messages de clé 103.

**1. Dans le terminal d'affichage, lancer Écran en premier :**

```sh
./bin/ecran
```

**2. Dans le second terminal, lancer le Shell :**

```sh
./bin/shell --disk disque.dat --format
```

L'option `--format` crée le disque et refuse d'écraser un fichier existant.
Le fichier `disque.dat` est créé dans le dossier depuis lequel cette commande
est lancée. Si elle est exécutée à la racine du projet, le disque se trouve
donc à côté du Makefile. Un chemin donné à `--disk` permet de choisir un autre
emplacement ; l'extension `.dat` est une convention de nommage.
Pour retrouver les données lors des sessions suivantes, relancer Écran,
puis le Shell sans cette option :

```sh
./bin/shell --disk disque.dat
```

Appuyer sur Ctrl+D dans le Shell pour fermer la session et terminer Écran.
Ctrl+C permet également de fermer les programmes.
Les données restent enregistrées dans `disque.dat`.

## Commandes

Les commandes ci-dessous agissent sur le disque virtuel. Le Shell du projet
prend en charge les commandes du sujet, dont la redirection `echo "texte" > fichier`.

| Commande | Rôle |
|---|---|
| `pwd` | Afficher le chemin absolu du répertoire courant |
| `ls [-l] [chemin]` | Lister les noms ; avec `-l`, ajouter les droits, le propriétaire, l'inode, la taille et la date de modification |
| `mkdir chemin` | Créer un répertoire |
| `rmdir chemin` | Supprimer un répertoire vide qui n'est pas utilisé comme chemin courant |
| `cd [chemin]` | Changer de répertoire ; sans chemin, revenir à la racine |
| `cp source destination` | Copier un fichier |
| `rm chemin` | Supprimer un fichier |
| `mv source destination` | Déplacer ou renommer un fichier ou un répertoire |
| `cat chemin` | Afficher le contenu d'un fichier texte |
| `echo "texte" > fichier` | Créer ou remplacer le contenu d'un fichier avec le texte suivi d'un retour à la ligne |
| `df` | Afficher l'utilisation des blocs et des inodes |
| `chmod u=rw,o=r chemin` | Modifier les droits du propriétaire et des autres ; réservé au propriétaire |

`ls` affiche les noms des entrées du répertoire, un par ligne.
`ls -l` ajoute les colonnes `Droits`, `UID`, `Inode`, `Taille(o)`,
`Date/heure` et `Nom`. Dans les deux modes, un `/` après le nom
indique un répertoire. Les noms commençant par un point sont également affichés.

La date est celle de la dernière modification du contenu, enregistrée dans
l'inode et conservée dans le disque `.dat`. Elle est affichée à l'heure locale,
au format `jour/mois/année heure:minute`. Pour un ancien fichier dont la date
n'était pas enregistrée, la colonne indique `inconnue` ; une nouvelle écriture
enregistrera sa date de modification.

Les chemins peuvent être absolus ou relatifs, avec `.` et `..`.
Mettre les noms ou les textes contenant des espaces entre guillemets.
`cp` et `mv` demandent le nom complet de la destination et refusent de l'écraser
si elle existe déjà.

Exemple à saisir dans le Shell, depuis la racine, si `cours` n'existe pas encore :

```text
pwd
mkdir cours
cd cours
pwd
```

Les deux commandes `pwd` affichent respectivement `/` puis `/cours` dans Écran.
Si le dossier existe déjà, passer directement à `cd cours` après le premier `pwd`.

## Droits : utilisateur et les autres

Chaque inode contient l'identifiant de son propriétaire et deux catégories de
droits : `u` pour cet utilisateur, `o` pour les autres. Il n'y a pas de groupe.
`r` autorise la lecture, `w` l'écriture et `x` la traversée d'un répertoire.
La création, la suppression et le déplacement d'une entrée demandent `w` et `x`
sur son répertoire parent. La suppression ne dépend donc pas du droit `w` du fichier.

Par défaut, un fichier a les droits `u=rw,o=r` et un répertoire `u=rwx,o=rx`.
`ls -l` affiche par exemple `-rw-r--` dans la colonne `Droits` : le premier caractère indique
le type, les trois suivants les droits de l'utilisateur et les trois derniers
ceux des autres. L'identifiant numérique du propriétaire est indiqué dans la colonne `UID`.

Le propriétaire peut modifier ces droits dans le Shell :

```text
chmod u=rw,o= note
chmod u=rw,o=r note
chmod u=rwx,o=rx cours
```

Le premier exemple retire tous les droits des autres sur `note`. Seul le
propriétaire peut faire un `chmod`, même si les autres ont le droit d'écrire.
Les accès sont vérifiés à l'ouverture du fichier et lors du parcours des
répertoires. Une ouverture déjà autorisée reste valable jusqu'à sa fermeture.

Par défaut, le Shell prend l'identifiant du compte Linux courant. L'option
`--user` sert à simuler un autre identifiant dans ce projet ; elle ne réalise
pas d'authentification Linux. Pour essayer les deux catégories, créer un disque
avec `--user 1000`, écrire un fichier et retirer les droits des autres :

```sh
./bin/shell --disk droits.dat --format --user 1000
```

```text
echo "Bonjour" > note
chmod u=rw,o= note
ls -l
```

Fermer avec Ctrl+D, relancer Écran, puis ouvrir le même disque avec une autre
identité, sans `--format` :

```sh
./bin/shell --disk droits.dat --user 1001
```

`cat note` doit être refusé. Pour rétablir la lecture des autres, relancer
la session avec `--user 1000` et saisir `chmod u=rw,o=r note`. Un nouvel essai
avec `--user 1001` doit alors permettre `cat note`, mais pas son écrasement.

Les anciens disques sans permissions sont convertis automatiquement : leurs
fichiers sont conservés, attribués à l'identité de la première ouverture avec
cette version, et reçoivent les droits par défaut.

## Fonctions du SGF

Les primitives sont déclarées dans `sgf/sgf.h`. Elles reçoivent un premier
paramètre `Sgf *fs` pour désigner le disque virtuel concerné.

| Fonction | Rôle |
|---|---|
| `_mycreat(fs, nom, mode)` | Créer et ouvrir un fichier vide, ou vider un fichier existant ; retourner son inode |
| `_myopen(fs, nom, mode)` | Ouvrir un fichier sans le vider ; en mode écriture, le créer s'il manque ; retourner son inode |
| `_myclose(fs, inode)` | Fermer l'ouverture du fichier |
| `_myread(fs, inode, buffer, nombre)` | Lire au plus `nombre` octets et retourner le nombre lu, ou 0 en fin de fichier |
| `_mywrite(fs, inode, buffer, nombre)` | Écrire `nombre` octets et retourner le nombre écrit |
| `_mkdir(fs, nom)` | Créer un répertoire |
| `_rmdir(fs, nom)` | Supprimer un répertoire vide qui n'est pas utilisé comme chemin courant |
| `_unlink(fs, nom)` | Retirer l'entrée d'un fichier et libérer son inode et ses blocs |

Les modes d'ouverture sont `SGF_READ`, `SGF_WRITE` ou `SGF_READ | SGF_WRITE`.
La lecture et l'écriture avancent dans le fichier depuis la position courante,
initialement à zéro. Une seule ouverture par inode est autorisée ; `_unlink`
refuse un fichier encore ouvert. Les erreurs sont signalées par un retour de -1.
Les modifications d'une commande sont sauvegardées ensemble. Pour les appels
directs aux primitives, fermer un fichier ouvert en écriture sauvegarde le disque ;
les opérations de répertoire et `_unlink` sauvegardent directement leurs changements.

Pour les appels directs à la bibliothèque, `sgf_set_user` sauvegarde d'abord
les fichiers ouverts en écriture, ferme les ouvertures et revient à la racine.
La fonction retourne 0 en cas de réussite. Si la sauvegarde échoue, elle
retourne -1 et conserve l'identité et les ouvertures de la session.

## Nettoyage et recompilation

Après une modification du code, fermer le Shell avec Ctrl+D, puis lancer
`make` dans le terminal Linux, depuis le dossier du projet :

```sh
make
```

Relancer Écran dans un terminal, puis le Shell dans l'autre, sans `--format`
pour conserver les données du disque existant :

```sh
./bin/shell --disk disque.dat
```

Utiliser le même nom de disque que lors de la session précédente.

Pour reconstruire entièrement les programmes après les avoir fermés :

```sh
make clean
make
```

`make clean` supprime uniquement `bin/` et `build/`. Le code et les fichiers
`.dat` sont conservés. Les programmes compilés et les disques créés pendant
l'utilisation sont exclus de Git par `.gitignore`.
