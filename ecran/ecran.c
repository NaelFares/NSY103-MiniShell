/* Affichage des resultats transmis par le Shell. */
#include "display.h"
#include <stdio.h>

/* Ecran est lance dans un second terminal et attend les messages. */
int main(void) {
    int queue = display_create(display_default_key());
    if (queue < 0) { perror("File de messages"); return 1; }
    fprintf(stderr, "Ecran pret.\n");
    char text[DISPLAY_CHUNK + 1];
    int result = 0;
    for (;;) {
        int r = display_receive(queue, text, sizeof text);
        if (r == 0) break;
        if (r < 0) { perror("Reception"); result = 1; break; }
        if (fputs(text, stdout) == EOF || fflush(stdout) == EOF) { result = 1; break; }
    }
    if (display_remove(queue) < 0) result = 1;
    return result;
}
