/* Commande df provisoire pour essayer Shell, Noyau et Ecran. */
#include "sgf.h"
#include <stdio.h>
#include <string.h>

/* La reponse doit traverser les deux tubes puis la file de messages. */
int sgf_execute(Sgf *fs, const char *command, char *output, size_t capacity) {
    (void)fs;
    char name[16], extra;
    if (sscanf(command, " %15s %c", name, &extra) == 1 && !strcmp(name, "df")) {
        snprintf(output, capacity, "Communication etablie ; stockage en preparation.\n");
        return 0;
    }
    snprintf(output, capacity, "Commande indisponible a cette etape.\n");
    return -1;
}
