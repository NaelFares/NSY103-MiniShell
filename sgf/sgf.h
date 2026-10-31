/* Declarations des fonctions du SGF accessibles au Noyau. */

#ifndef SGF_H
#define SGF_H

#include <stddef.h>

typedef struct Sgf Sgf;

/* Modes d'ouverture : lecture, ecriture, ou SGF_READ | SGF_WRITE. */
#define SGF_READ 1
#define SGF_WRITE 2

/* Creation/ouverture : inode positif si reussite, -1 en cas d'erreur.
 * _mycreat vide un fichier existant. _myopen le conserve et le cree si
 * necessaire en mode ecriture. Une seule ouverture par inode est autorisee. */
int _mycreat(Sgf *fs, const char *name, int mode);
int _myopen(Sgf *fs, const char *name, int mode);

/* Fermeture : 0 si reussite, -1 en cas d'erreur. */
int _myclose(Sgf *fs, int id);

/* Lecture/ecriture a la position courante : nombre d'octets traites,
 * 0 pour une fin de lecture ou une demande vide, -1 en cas d'erreur. */
int _myread(Sgf *fs, int id, void *buffer, size_t count);
int _mywrite(Sgf *fs, int id, const void *buffer, size_t count);

/* Creation/suppression : 0 si reussite, -1 en cas d'erreur. */
int _mkdir(Sgf *fs, const char *name);
int _rmdir(Sgf *fs, const char *name);
int _unlink(Sgf *fs, const char *name);

/* Avec create=1, on cree un nouveau disque sans ecraser un fichier existant. */
Sgf *sgf_open(const char *path, int create, char *error, size_t capacity);
void sgf_close(Sgf *fs);

/* Identites simulees pour verifier les droits utilisateur/autres. */
Sgf *sgf_open_user(const char *path, int create, char *error, size_t capacity, unsigned user_id);

/* Sauvegarder les fichiers ouverts en ecriture, puis changer d'identite.
 * Retour : 0 si reussite, -1 si la sauvegarde echoue ; l'etat est alors conserve. */
int sgf_set_user(Sgf *fs, unsigned user_id);

/* Seul le proprietaire peut changer les droits ; r=4, w=2, x=1. */
int sgf_chmod(Sgf *fs, const char *name, unsigned user_rights, unsigned other_rights);

/* Retour : 0 si tout va bien, -1 en cas d'erreur. Le texte se termine toujours par un octet nul. */
int sgf_execute(Sgf *fs, const char *command, char *output, size_t capacity);

#endif
