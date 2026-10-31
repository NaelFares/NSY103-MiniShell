/* Gestion des commandes recues et de leur execution par le SGF. */

#define _POSIX_C_SOURCE 200809L
#include "kernel.h"
#include "protocol.h"
#include "sgf.h"
#include <stdlib.h>

/* On ouvre le disque, puis on traite les commandes jusqu'a la fin de session. */
int kernel_run(int requests, int responses, const char *disk, int create, unsigned user_id) {
    char error[512];
    Sgf *fs = sgf_open_user(disk, create, error, sizeof error, user_id);
    if (!fs) {
        frame_send(responses, error, 1);
        return 1;
    }
    if (frame_send(responses, "Disque pret.\n", 0) < 0) {
        sgf_close(fs);
        return 1;
    }
    char command[COMMAND_MAX];
    char *output = malloc(OUTPUT_MAX);
    if (!output) {
        sgf_close(fs);
        return 1;
    }
    int result = 0;
    for (;;) {
        int status, r = frame_recv(requests, command, sizeof command, &status);
        if (r <= 0) {
            if (r < 0)
                result = 1;
            break;
        }
        if (status) {
            result = 1;
            break;
        }
        /* Le SGF fait le travail sur les fichiers ; le Noyau transmet le resultat. */
        r = sgf_execute(fs, command, output, OUTPUT_MAX);
        if (frame_send(responses, *output ? output : "OK\n", r) < 0) {
            result = 1;
            break;
        }
    }
    free(output);
    sgf_close(fs);
    return result;
}
