/* Declarations des fonctions de gestion de la file de messages. */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <stddef.h>

#define DISPLAY_CHUNK 512u
#define DISPLAY_TEXT 1u
#define DISPLAY_END 2u
key_t display_default_key(void);
int display_create(key_t key);
int display_connect(key_t key);

/* Retour : 0 si les messages sont envoyes, -1 en cas d'erreur. */
int display_send(int queue, const char *text, int end);

/* Retour : 1 pour du texte, 0 pour la fin, -1 pour une erreur (y compris EINTR). */
int display_receive(int queue, char *text, size_t capacity);
int display_remove(int queue);

#endif
