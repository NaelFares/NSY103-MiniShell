#ifndef DISPLAY_H
#define DISPLAY_H
#include <sys/types.h>
#include <sys/ipc.h>
#include <stddef.h>
#define DISPLAY_CHUNK 512u
#define DISPLAY_TEXT 1u
#define DISPLAY_END 2u
key_t display_default_key(void);
int display_parse_key(const char *text,key_t *key);
int display_create(key_t key);
int display_connect(key_t key);
/* Bounded wait on a full queue; never silently discard output. */
int display_send(int queue,const char *text,int end);
/* 1=text, 0=end, -1=error. EINTR is returned to allow signal handling. */
int display_receive(int queue,char *text,size_t capacity);
int display_remove(int queue);
#endif
