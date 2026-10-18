/* Organisation interne du disque et declarations des fonctions de stockage. */

#ifndef STORAGE_H
#define STORAGE_H

#include "sgf.h"
#include <stdint.h>
#include <stdio.h>

/* Tailles et limites choisies pour notre disque virtuel. */
#define BLOCK_SIZE 512u
#define BLOCK_COUNT 2048u
#define INODE_COUNT 128u
#define INODE_SIZE 64u
#define DIRECT_COUNT 8u
#define FILE_BLOCKS (DIRECT_COUNT + BLOCK_SIZE / 4u)
#define FILE_MAX (FILE_BLOCKS * BLOCK_SIZE)
#define DIR_COUNT 64u
#define NAME_MAX_BYTES 59u

/* Positions des zones du disque : occupation, inodes, puis donnees. */
#define MAP_I 512u
#define MAP_B 1024u
#define TABLE_I 1536u
#define DATA_START (TABLE_I + INODE_COUNT * INODE_SIZE)
#define IMAGE_SIZE (DATA_START + BLOCK_COUNT * BLOCK_SIZE)
#define KIND_FILE 1u
#define KIND_DIR 2u

/* Etat du disque pendant la session : contenu en memoire, fichier et repertoire courant. */
struct Sgf {
    unsigned char *image;
    FILE *file;
    uint32_t cwd;
};

/* Fonctions internes partagees entre le stockage et les commandes. */
uint32_t disk_get(const unsigned char *p);
void disk_put(unsigned char *p, uint32_t value);
unsigned char *inode(Sgf *fs, uint32_t id);
uint32_t inode_alloc(Sgf *fs, uint32_t kind, uint32_t parent);
void inode_free(Sgf *fs, uint32_t id);
int file_read(Sgf *fs, uint32_t id, unsigned char *out, size_t capacity);
int file_write(Sgf *fs, uint32_t id, const unsigned char *data, size_t size);
int disk_save(Sgf *fs);

#endif
