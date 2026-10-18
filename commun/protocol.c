/* Gestion des messages transmis dans les tubes. */

#include "protocol.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

/* Les deux processus echangent un en-tete simple, puis le texte. */
typedef struct {
    unsigned length;
    int status;
} Header;

/* Un write peut etre incomplet : on continue jusqu'a avoir tout envoye. */
int write_full(int fd, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

/* On attend tous les octets demandes et on repere un message coupe. */
static int read_full(int fd, void *data, size_t size, int header) {
    unsigned char *p = data;
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, p + done, size - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -1;
        if (n == 0) {
            if (header && done == 0)
                return 0;
            errno = EPROTO;
            return -1;
        }
        done += (size_t)n;
    }
    return 1;
}

/* On envoie d'abord les informations du message, puis son texte. */
int frame_send(int fd, const char *text, int status) {
    size_t len = strlen(text);
    if (len >= OUTPUT_MAX) {
        errno = EMSGSIZE;
        return -1;
    }
    Header h = {0};
    h.length = (unsigned)len;
    h.status = status ? 1 : 0;
    if (write_full(fd, &h, sizeof h) < 0)
        return -1;
    return write_full(fd, text, len);
}

/* On verifie l'en-tete avant de recuperer le texte et le resultat. */
int frame_recv(int fd, char *text, size_t capacity, int *status) {
    Header h;
    int r = read_full(fd, &h, sizeof h, 1);
    if (r <= 0)
        return r;
    unsigned len = h.length;
    if (len >= capacity || (h.status != 0 && h.status != 1)) {
        errno = EPROTO;
        return -1;
    }
    if (read_full(fd, text, len, 0) < 0)
        return -1;
    if (memchr(text, 0, len)) {
        errno = EPROTO;
        return -1;
    }
    text[len] = 0;
    *status = h.status;
    return 1;
}
