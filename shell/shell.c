/* Gestion de la saisie des commandes et des echanges avec le Noyau et Ecran. */

#define _POSIX_C_SOURCE 200809L
#include "display.h"
#include "kernel.h"
#include "protocol.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t stopped = 0;

/* On signale l'arret ; le nettoyage sera fait dans la boucle principale. */
static void stop(int sig) {
    (void)sig;
    stopped = 1;
}

/* On lance le Noyau, puis on transmet les commandes jusqu'a Ctrl+D ou Ctrl+C. */
int main(int argc, char **argv) {
    const char *disk = "disque.dat";
    int create = 0;
    key_t key = display_default_key();

    /* On lit les options donnees au lancement du Shell. */
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--format"))
            create = 1;
        else if (!strcmp(argv[i], "--disk") && i + 1 < argc)
            disk = argv[++i];
        else {
            fprintf(stderr, "Usage : %s [--disk fichier.dat] [--format]\n", argv[0]);
            return 2;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    struct sigaction action = {0};
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    int queue = display_connect(key);
    if (queue < 0) {
        fprintf(stderr, "Lancez Ecran avant le Shell (cle %ld) : %s\n", (long)key, strerror(errno));
        return 1;
    }
    /* Un tube sert aux commandes, l'autre aux reponses du Noyau. */
    int request[2], response[2];
    if (pipe(request) < 0) {
        perror("pipe commandes");
        return 1;
    }
    if (pipe(response) < 0) {
        perror("pipe reponses");
        close(request[0]);
        close(request[1]);
        return 1;
    }

    /* Le fils execute le Noyau ; le pere continue la saisie dans le Shell. */
    pid_t child = fork();
    if (child == 0) {
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);
        close(request[1]);
        close(response[0]);
        int r = kernel_run(request[0], response[1], disk, create);
        close(request[0]);
        close(response[1]);
        _exit(r);
    }
    close(request[0]);
    close(response[1]);
    if (child < 0) {
        perror("fork");
        close(request[1]);
        close(response[0]);
        return 1;
    }
    int result = 0, status = 0;
    char *output = malloc(OUTPUT_MAX);
    char line[COMMAND_MAX];
    if (!output) {
        result = 1;
        goto cleanup;
    }

    /* On attend que le Noyau ait termine l'ouverture du disque. */
    if (frame_recv(response[0], output, OUTPUT_MAX, &status) != 1) {
        perror("Initialisation Noyau");
        result = 1;
        goto cleanup;
    }
    if (display_send(queue, output, 0) < 0) {
        perror("Affichage");
        result = 1;
        goto cleanup;
    }
    if (status) {
        fprintf(stderr, "%s\n", output);
        result = 1;
        goto cleanup;
    }
    fprintf(stderr, "Mon D.O.S. : Ctrl+D ou Ctrl+C pour terminer.\n");

    /* On lit une commande, on attend sa reponse, puis on l'envoie a Ecran. */
    while (!stopped) {
        if (isatty(STDIN_FILENO)) {
            fputs("MonDOS> ", stderr);
            fflush(stderr);
        }
        const char *command = line;
        if (!fgets(line, sizeof line, stdin)) {
            if (ferror(stdin) && !stopped) {
                perror("Lecture commande");
                result = 1;
            }
            break;
        } else {
            /* Une ligne trop longue est rejetee en entier. */
            size_t length = strlen(line);
            if (length == sizeof line - 1 && line[length - 1] != '\n') {
                int c;
                while ((c = getchar()) != '\n' && c != EOF && !stopped) {}
                if (display_send(queue, "Erreur : commande trop longue\n", 0) < 0)
                    break;
                result = 1;
                continue;
            }
            size_t i = 0;
            while (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n')
                ++i;
            if (!line[i])
                continue;
        }
        if (frame_send(request[1], command, 0) < 0 ||
            frame_recv(response[0], output, OUTPUT_MAX, &status) != 1) {
            fprintf(stderr, "Communication avec le Noyau interrompue\n");
            result = 1;
            break;
        }
        if (display_send(queue, output, 0) < 0) {
            perror("Affichage");
            result = 1;
            break;
        }
        if (status)
            result = 1;
    }
cleanup:
    /* Fermer le tube permet aussi au Noyau de sortir de son attente. */
    close(request[1]);
    close(response[0]);
    free(output);

    /* On attend la fin du fils avant de fermer la session. */
    int child_status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &child_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(child_status) || WEXITSTATUS(child_status))
        result = 1;
    if (display_send(queue, "", 1) < 0) {
        perror("Fin affichage");
        result = 1;
    }
    return stopped ? 128 + SIGINT : result;
}
