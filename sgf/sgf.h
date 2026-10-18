/* Declarations des fonctions du SGF accessibles au Noyau. */

#ifndef SGF_H
#define SGF_H

#include <stddef.h>

typedef struct Sgf Sgf;

/* Avec create=1, on cree un nouveau disque sans ecraser un fichier existant. */
Sgf *sgf_open(const char *path, int create, char *error, size_t capacity);
void sgf_close(Sgf *fs);

/* Retour : 0 si tout va bien, -1 en cas d'erreur. Le texte se termine toujours par un octet nul. */
int sgf_execute(Sgf *fs, const char *command, char *output, size_t capacity);

#endif
