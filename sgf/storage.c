/* Initialisation provisoire du SGF pour essayer les communications. */
#include "storage.h"
#include <stdlib.h>
#include <stdio.h>

/* Le stockage sur disque sera ajoute a l'etape suivante. */
Sgf *sgf_open(const char *path, int create, char *error, size_t capacity) {
    (void)path;
    (void)create;
    Sgf *fs = calloc(1, sizeof *fs);
    if (!fs)
        snprintf(error, capacity, "Memoire insuffisante");
    return fs;
}

/* Liberation du contexte de cette premiere etape. */
void sgf_close(Sgf *fs) {
    free(fs);
}
