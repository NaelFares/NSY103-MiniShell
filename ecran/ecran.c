/* Gestion de la reception et de l'affichage des resultats du Shell. */

#define _POSIX_C_SOURCE 200809L
#include "display.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t stopped = 0;

/* On signale l'arret ; le nettoyage sera fait dans la boucle principale. */
static void stop(int sig) {
    (void)sig;
    stopped = 1;
}

/* On recoit les resultats jusqu'a la fermeture du Shell ou une interruption. */
int main(int argc, char **argv) {
    key_t key = display_default_key();
    if (argc != 1) {
        fprintf(stderr, "Usage : %s\n", argv[0]);
        return 2;
    }

    /* Ctrl+C demande un arret propre du programme. */
    struct sigaction action = {0};
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    /* Ecran doit etre lance en premier pour creer la file. */
    int queue = display_create(key);
    if (queue < 0) {
        fprintf(stderr, "Ecran : %s (cle %ld ; verifier ipcs -q)\n", strerror(errno), (long)key);
        return 1;
    }
    fprintf(stderr, "Ecran pret, cle %ld. Lancez le Shell dans le terminal 1.\n", (long)key);
    char text[DISPLAY_CHUNK + 1];
    int result = 0;

    /* On attend les messages et on affiche le texte au fur et a mesure. */
    while (!stopped) {
        int r = display_receive(queue, text, sizeof text);
        if (r == 0)
            break;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "Ecran : %s\n", strerror(errno));
            result = 1;
            break;
        }
        if (fputs(text, stdout) == EOF || fflush(stdout) == EOF) {
            result = 1;
            break;
        }
    }

    /* A la fin, on retire la file de messages de Linux. */
    if (display_remove(queue) < 0 && errno != EINVAL && errno != EIDRM)
        result = 1;
    return result;
}
