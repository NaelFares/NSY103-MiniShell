/* Relais entre les tubes du Shell et les fonctions du SGF. */
#include "kernel.h"
#include "protocol.h"
#include "sgf.h"
#include <stdlib.h>

/* Ouverture du SGF et traitement des commandes jusqu'a la fermeture du tube. */
int kernel_run(int requests, int responses, const char *disk, int create) {
    char error[512];
    Sgf *fs = sgf_open(disk, create, error, sizeof error);
    if (!fs) { frame_send(responses, error, 1); return 1; }
    char *output = malloc(OUTPUT_MAX);
    if (!output) { sgf_close(fs); return 1; }
    int result = 0;
    if (frame_send(responses, "Disque pret.\n", 0) < 0) result = 1;
    char command[COMMAND_MAX];
    while (result == 0) {
        int status;
        int received = frame_recv(requests, command, sizeof command, &status);
        if (received <= 0) break;
        int code = sgf_execute(fs, command, output, OUTPUT_MAX);
        if (frame_send(responses, *output ? output : "OK\n", code) < 0) result = 1;
    }
    free(output);
    sgf_close(fs);
    return result;
}
