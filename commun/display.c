/* Gestion des echanges avec Ecran par une file de messages. */

#include "display.h"
#include <errno.h>
#include <string.h>
#include <sys/msg.h>

/* Meme structure que les exemples msgsend et msgrcv du cours. */
typedef struct {
    long type;
    char text[DISPLAY_CHUNK + 1];
} Message;

/* Les deux programmes utilisent la meme cle pour retrouver la file. */
key_t display_default_key(void) {
    return (key_t)103;
}

/* Creation de la file par Ecran. Une seule session est prevue. */
int display_create(key_t key) {
    return msgget(key, IPC_CREAT | IPC_EXCL | 0600);
}

/* Recuperation de la file deja creee par Ecran. */
int display_connect(key_t key) {
    return msgget(key, 0600);
}

/* Les longs resultats sont envoyes en plusieurs messages. */
int display_send(int queue, const char *text, int end) {
    size_t length = strlen(text), pos = 0;
    while (pos < length) {
        Message msg;
        msg.type = DISPLAY_TEXT;
        size_t n = length - pos;
        if (n > DISPLAY_CHUNK)
            n = DISPLAY_CHUNK;
        memcpy(msg.text, text + pos, n);
        msg.text[n] = 0;
        /* Si la file est pleine, on attend qu'Ecran lise les messages. */
        if (msgsnd(queue, &msg, n + 1, 0) < 0)
            return -1;
        pos += n;
    }
    if (end) {
        Message msg;
        msg.type = DISPLAY_END;
        msg.text[0] = 0;
        return msgsnd(queue, &msg, 1, 0);
    }
    return 0;
}

/* Attente d'un message : type 0 conserve l'ordre d'arrivee. */
int display_receive(int queue, char *text, size_t capacity) {
    Message msg;
    ssize_t n = msgrcv(queue, &msg, sizeof msg.text, 0, 0);
    if (n < 0)
        return -1;
    if (n < 1 || (size_t)n > capacity || msg.text[n - 1] != 0 ||
        (msg.type != DISPLAY_TEXT && msg.type != DISPLAY_END)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(text, msg.text, (size_t)n);
    return msg.type == DISPLAY_END ? 0 : 1;
}

/* Suppression de la file a la fermeture d'Ecran. */
int display_remove(int queue) {
    return msgctl(queue, IPC_RMID, NULL);
}
