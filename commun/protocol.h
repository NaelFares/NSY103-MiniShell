/* Format des messages et declarations des fonctions de communication par tubes. */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#define COMMAND_MAX 4096u
#define OUTPUT_MAX 131072u
/* Envoi du resultat et de la longueur du texte, puis du texte. */
int frame_send(int fd, const char *text, int status);

/* Retour : 1 pour un message, 0 pour une fin normale, -1 pour une erreur. */
int frame_recv(int fd, char *text, size_t capacity, int *status);
int write_full(int fd, const void *data, size_t size);

#endif
