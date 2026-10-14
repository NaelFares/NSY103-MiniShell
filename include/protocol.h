#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define COMMAND_MAX 4096u
#define OUTPUT_MAX 131072u
#define FRAME_MAGIC 0x4d444f53u
/* Header: magic, version, payload length, status. All fields uint32 LE. */
int frame_send(int fd, const char *text, int status);
/* 1 = frame, 0 = orderly EOF, -1 = error (including truncated frame). */
int frame_recv(int fd, char *text, size_t capacity, int *status);
int write_full(int fd, const void *data, size_t size);
#endif
