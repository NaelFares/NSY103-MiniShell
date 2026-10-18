/* Echange de petits resultats entre le Shell et Ecran. */
#include "display.h"
#include <errno.h>
#include <string.h>
#include <sys/msg.h>

typedef struct {
    long type;
    char text[DISPLAY_CHUNK + 1];
} Message;

/* Meme cle dans les deux programmes. */
key_t display_default_key(void) { return (key_t)103; }
/* Creation de la file par Ecran. */
int display_create(key_t key) { return msgget(key, IPC_CREAT | IPC_EXCL | 0600); }
/* Recuperation de la file par le Shell. */
int display_connect(key_t key) { return msgget(key, 0600); }

/* Cette premiere version limite chaque resultat a DISPLAY_CHUNK octets. */
int display_send(int queue, const char *text, int end) {
    Message message;
    size_t length = strlen(text);
    if (length > DISPLAY_CHUNK) {
        errno = EMSGSIZE;
        return -1;
    }
    message.type = end ? DISPLAY_END : DISPLAY_TEXT;
    memcpy(message.text, text, length + 1);
    return msgsnd(queue, &message, length + 1, 0);
}

/* Reception bloquante, dans l'ordre d'arrivee. */
int display_receive(int queue, char *text, size_t capacity) {
    Message message;
    ssize_t n = msgrcv(queue, &message, sizeof message.text, 0, 0);
    if (n < 0) return -1;
    if (n == 0 || (size_t)n > capacity || message.text[n - 1] != 0) {
        errno = EINVAL;
        return -1;
    }
    memcpy(text, message.text, (size_t)n);
    return message.type == DISPLAY_END ? 0 : 1;
}

/* Destruction de la file en fin de session. */
int display_remove(int queue) { return msgctl(queue, IPC_RMID, NULL); }
