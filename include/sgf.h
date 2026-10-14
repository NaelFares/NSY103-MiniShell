#ifndef SGF_H
#define SGF_H
#include <stddef.h>
typedef struct Sgf Sgf;
/* create=1 exclusively creates a new disk; never overwrites one. */
Sgf *sgf_open(const char *path, int create, char *error, size_t capacity);
void sgf_close(Sgf *fs);
/* Returns 0 on success, -1 on error. Output is always NUL terminated. */
int sgf_execute(Sgf *fs, const char *command, char *output, size_t capacity);
#endif
