# VM de développement NSY103

> Note du 27 octobre 2026 : ce guide décrit la préparation initiale du
> 8 octobre. Le code est désormais réalisé ; consulter `README.md` pour
> compiler et lancer les deux terminaux.

## Configuration retenue

- Nom VirtualBox : **NSY103-Debian-Dev**.
- Debian 13.7, 64 bits, bureau Xfce, français, fuseau Europe/Paris.
- 2 processeurs virtuels, 2 Go de RAM, disque VDI dynamique de 25 Go.
- Réseau NAT pour les téléchargements, sans exposition directe sur le réseau local.
- Fichiers de la VM : `C:\Users\nesri\VirtualBox VMs\NSY103-Debian-Dev` (hors OneDrive).
- Compte : `etudiant`. Le mot de passe est dans `ACCES-VM.txt`, dans le dossier de la VM. Pour le changer : `passwd` dans Linux.

## Pourquoi ces choix

Le rapport prévoit un Shell parent, un Noyau fils, deux tubes anonymes, une bibliothèque SGF, un fichier `.dat` et un programme Écran dans un second terminal. Une VM Linux permet de travailler avec les appels système Unix correspondant à cette architecture. Debian et Xfce constituent ici un choix d'environnement de développement ; le rapport n'impose pas de distribution ni de bureau.

Les outils C sont préparés en cohérence avec ces appels système ; le rapport ne fixe pas explicitement le langage. Il ne tranche pas non plus entre une file de messages System V (`msgget`, `msgsnd`, `msgrcv`) et POSIX (`mq_open`, `mq_send`, `mq_receive`) : les deux sont pris en charge. Le groupe devra convenir de l'API à employer.

Le « Noyau » du projet sera un programme utilisateur Linux. Le disque `.dat` sera un fichier ordinaire : il est distinct du disque VDI contenant Debian, et ne nécessite ni partition supplémentaire ni accès administrateur.

Le PC possède environ 16 Go de mémoire et 4 cœurs / 8 processeurs logiques. Au moment de la préparation, environ 3 Go de mémoire étaient libres ; 2 Go sont donc attribués à la VM. Si nécessaire, fermer des applications Windows avant de l'utiliser. Le disque de 25 Go grossit progressivement selon les données réellement écrites.

## Utilisation

1. Ouvrir VirtualBox, sélectionner **NSY103-Debian-Dev**, puis **Démarrer** ou **Afficher**.
2. Se connecter avec le compte `etudiant`.
3. Travailler dans `/home/etudiant/NSY103-MiniShell`.
4. Utiliser le raccourci **NSY103 - Deux terminaux** du bureau pour ouvrir les terminaux Shell et Écran. Si le bureau demande d'autoriser ce raccourci, sélectionner l'autorisation de lancement.
5. Compiler et exécuter le projet dans Linux. Aucun programme du projet n'est encore implémenté : le dossier fourni initialement contient uniquement le rapport.
6. Éteindre Debian depuis le menu du bureau après utilisation.

Outils installés : GCC, Make, GDB, Valgrind, Git, strace, manuels de programmation, tmux, éditeur Mousepad et terminal Xfce.

## Vérification indépendante du projet

Le dossier `/home/etudiant/Outils-NSY103` contient un test de l'environnement, distinct du futur code du groupe.

Pour relancer les vérifications :

```sh
~/Outils-NSY103/verifier.sh
```

Le test compile en C11 avec les avertissements activés et vérifie : création d'un processus fils, aller-retour par deux tubes, envoi/réception via les deux API de files de messages, lecture/écriture à une position donnée dans un fichier, puis exécution sous Valgrind. Les ressources temporaires sont supprimées par le test.

## Travail en groupe

Le code sera conservé dans le système de fichiers Linux de la VM. Pour collaborer, utiliser un dépôt Git commun quand le groupe l'aura choisi. Configurer individuellement son nom et son adresse Git ; aucun compte ni dépôt distant n'est configuré automatiquement.

Décisions restant à prendre ensemble : langage définitif, format des échanges dans les tubes, API et format des messages, signatures du SGF et structure des inodes, conformément au rapport.
## Validation et point de restauration

Installation et tests terminés le 8 octobre 2026. Les quatre vérifications ont réussi et Valgrind a signalé zéro erreur et zéro fuite sur le programme de test. Résultats de préparation dans `~/Outils-NSY103/verification.log` dans Debian.

Un point de restauration VirtualBox **Environnement-NSY103-pret** a été créé avec la VM éteinte. Il permet de retrouver l'environnement initial ; le restaurer annule les modifications ultérieures de la VM, donc sauvegarder d'abord le travail à conserver.

L'image Debian provient du site officiel (https://www.debian.org/download.en.html) et son empreinte SHA-512 a été comparée au fichier SHA512SUMS du serveur de téléchargement.
