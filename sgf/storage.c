/* Gestion des blocs, des inodes et de la sauvegarde du disque virtuel. */

#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* On lit un entier de quatre octets dans l'image du disque. */
uint32_t disk_get(const unsigned char *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

/* On ecrit un entier de quatre octets dans l'image du disque. */
void disk_put(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (8 * i));
}

/* Un bit a 1 indique que l'emplacement est occupe. */
static int bit(const unsigned char *m, uint32_t id) {
    return (m[id / 8] >> (id % 8)) & 1;
}

/* On marque un emplacement comme libre ou occupe. */
static void mark(unsigned char *m, uint32_t id, int used) {
    unsigned char mask = (unsigned char)(1u << (id % 8));
    if (used)
        m[id / 8] |= mask;
    else
        m[id / 8] &= (unsigned char)~mask;
}

/* On retrouve la fiche du fichier dans la table des inodes. */
unsigned char *inode(Sgf *fs, uint32_t id) {
    return fs->image + TABLE_I + id * INODE_SIZE;
}

/* On retrouve le debut d'un bloc de donnees. */
static unsigned char *block(Sgf *fs, uint32_t id) {
    return fs->image + DATA_START + id * BLOCK_SIZE;
}

/* On cherche un bloc libre, en laissant le bloc 0 reserve, puis on le remet a zero. */
static uint32_t block_alloc(Sgf *fs) {
    for (uint32_t i = 1; i < BLOCK_COUNT; ++i)
        if (!bit(fs->image + MAP_B, i)) {
            mark(fs->image + MAP_B, i, 1);
            memset(block(fs, i), 0, BLOCK_SIZE);
            return i;
        }
    errno = ENOSPC;
    return 0;
}

/* On reserve une fiche pour un nouveau fichier ou repertoire. */
uint32_t inode_alloc(Sgf *fs, uint32_t kind, uint32_t parent) {
    for (uint32_t i = 1; i < INODE_COUNT; ++i)
        if (!bit(fs->image + MAP_I, i)) {
            mark(fs->image + MAP_I, i, 1);
            memset(inode(fs, i), 0, INODE_SIZE);
            disk_put(inode(fs, i), kind);
            disk_put(inode(fs, i) + 8, parent);
            return i;
        }
    errno = ENOSPC;
    return 0;
}

/* Apres les huit references directes, on passe par le bloc d'indirection. */
static uint32_t data_block(Sgf *fs, unsigned char *node, uint32_t index) {
    if (index < DIRECT_COUNT)
        return disk_get(node + 12 + index * 4);
    uint32_t indirect = disk_get(node + 44);
    return indirect ? disk_get(block(fs, indirect) + (index - DIRECT_COUNT) * 4) : 0;
}

/* On libere tous les blocs du fichier, y compris son bloc d'indirection. */
static void release_data(Sgf *fs, unsigned char *node) {
    uint32_t count = (disk_get(node + 4) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (uint32_t i = 0; i < count; ++i)
        mark(fs->image + MAP_B, data_block(fs, node, i), 0);
    uint32_t indirect = disk_get(node + 44);
    if (indirect)
        mark(fs->image + MAP_B, indirect, 0);
    memset(node + 12, 0, 36);
    disk_put(node + 4, 0);
}

/* On libere les donnees, puis la fiche du fichier. */
void inode_free(Sgf *fs, uint32_t id) {
    release_data(fs, inode(fs, id));
    memset(inode(fs, id), 0, INODE_SIZE);
    mark(fs->image + MAP_I, id, 0);
}

/* On reconstitue le contenu du fichier en suivant ses references de blocs. */
int file_read(Sgf *fs, uint32_t id, unsigned char *out, size_t capacity) {
    unsigned char *node = inode(fs, id);
    size_t size = disk_get(node + 4);
    if (size > capacity) {
        errno = EFBIG;
        return -1;
    }
    for (size_t pos = 0; pos < size; pos += BLOCK_SIZE) {
        size_t n = size - pos;
        if (n > BLOCK_SIZE)
            n = BLOCK_SIZE;
        memcpy(out + pos, block(fs, data_block(fs, node, (uint32_t)(pos / BLOCK_SIZE))), n);
    }
    return 0;
}

/* On garde les blocs existants et on adapte seulement ceux qui sont necessaires. */
int file_write(Sgf *fs, uint32_t id, const unsigned char *data, size_t size) {
    if (size > FILE_MAX) {
        errno = EFBIG;
        return -1;
    }
    unsigned char *node = inode(fs, id);
    uint32_t old_count = (disk_get(node + 4) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t new_count = (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    /* On verifie la place disponible avant de modifier les donnees ou les references. */
    uint32_t needed = new_count > old_count ? new_count - old_count : 0;
    if (new_count > DIRECT_COUNT && !disk_get(node + 44))
        ++needed;
    uint32_t available = 0;
    for (uint32_t i = 1; i < BLOCK_COUNT; ++i)
        if (!bit(fs->image + MAP_B, i))
            ++available;
    if (needed > available) {
        errno = ENOSPC;
        return -1;
    }
    if (new_count > DIRECT_COUNT && !disk_get(node + 44)) {
        uint32_t b = block_alloc(fs);
        if (!b)
            return -1;
        disk_put(node + 44, b);
    }
    /* On ajoute les blocs manquants et on libere uniquement ceux devenus inutiles. */
    for (uint32_t i = old_count; i < new_count; ++i) {
        uint32_t b = block_alloc(fs);
        if (!b)
            return -1;
        if (i < DIRECT_COUNT)
            disk_put(node + 12 + i * 4, b);
        else
            disk_put(block(fs, disk_get(node + 44)) + (i - DIRECT_COUNT) * 4, b);
    }
    for (uint32_t i = new_count; i < old_count; ++i) {
        mark(fs->image + MAP_B, data_block(fs, node, i), 0);
        if (i < DIRECT_COUNT)
            disk_put(node + 12 + i * 4, 0);
        else
            disk_put(block(fs, disk_get(node + 44)) + (i - DIRECT_COUNT) * 4, 0);
    }
    if (new_count <= DIRECT_COUNT && disk_get(node + 44)) {
        mark(fs->image + MAP_B, disk_get(node + 44), 0);
        disk_put(node + 44, 0);
    }
    for (size_t pos = 0; pos < size; pos += BLOCK_SIZE) {
        unsigned char *dest = block(fs, data_block(fs, node, (uint32_t)(pos / BLOCK_SIZE)));
        size_t n = size - pos;
        if (n > BLOCK_SIZE)
            n = BLOCK_SIZE;
        if (memcmp(dest, data + pos, n))
            memcpy(dest, data + pos, n);
        if (n < BLOCK_SIZE)
            memset(dest + n, 0, BLOCK_SIZE - n);
    }
    disk_put(node + 4, (uint32_t)size);
    return 0;
}

/* Sauvegarde du disque avec les fonctions de fichiers binaires du cours. */
int disk_save(Sgf *fs) {
    if (fseek(fs->file, 0, SEEK_SET) != 0)
        return -1;
    if (fwrite(fs->image, 1, IMAGE_SIZE, fs->file) != IMAGE_SIZE)
        return -1;
    return fflush(fs->file);
}

/* On refuse un disque incoherent avant de l'utiliser. */
static int validate(Sgf *fs) {
    const unsigned char *im = fs->image;
    if (memcmp(im, "MDOSv001", 8) || disk_get(im + 8) != BLOCK_SIZE ||
        disk_get(im + 12) != BLOCK_COUNT || disk_get(im + 16) != INODE_COUNT ||
        disk_get(im + 20) != MAP_I || disk_get(im + 24) != MAP_B || disk_get(im + 28) != TABLE_I ||
        disk_get(im + 32) != DATA_START)
        return -1;
    if (!bit(im + MAP_I, 0) || !bit(im + MAP_B, 0) || disk_get(inode(fs, 0)) != KIND_DIR ||
        disk_get(inode(fs, 0) + 8) != 0)
        return -1;
    unsigned char seen[BLOCK_COUNT] = {0};
    seen[0] = 1;
    for (uint32_t id = 0; id < INODE_COUNT; ++id)
        if (bit(im + MAP_I, id)) {
            unsigned char *node = inode(fs, id);
            uint32_t kind = disk_get(node), size = disk_get(node + 4);
            uint32_t parent = disk_get(node + 8);
            if ((kind != KIND_FILE && kind != KIND_DIR) || size > FILE_MAX ||
                (kind == KIND_DIR && size != DIR_COUNT * 64) || parent >= INODE_COUNT ||
                !bit(im + MAP_I, parent) || disk_get(inode(fs, parent)) != KIND_DIR)
                return -1;
            uint32_t indirect = disk_get(node + 44), count = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
            if (count > DIRECT_COUNT) {
                if (!indirect || indirect >= BLOCK_COUNT || seen[indirect] ||
                    !bit(im + MAP_B, indirect))
                    return -1;
                seen[indirect] = 1;
            } else if (indirect)
                return -1;
            for (uint32_t i = 0; i < FILE_BLOCKS; ++i) {
                if (i >= DIRECT_COUNT && !indirect)
                    break;
                uint32_t b = data_block(fs, node, i);
                if (i >= count) {
                    if (b)
                        return -1;
                    continue;
                }
                if (!b || b >= BLOCK_COUNT || seen[b] || !bit(im + MAP_B, b))
                    return -1;
                seen[b] = 1;
            }
        }
    for (uint32_t i = 0; i < BLOCK_COUNT; ++i)
        if (bit(im + MAP_B, i) != seen[i])
            return -1;
    /* On verifie les noms, les parents et les liens entre les repertoires et les fichiers. */
    unsigned refs[INODE_COUNT] = {0};
    for (uint32_t id = 0; id < INODE_COUNT; ++id)
        if (bit(im + MAP_I, id) && disk_get(inode(fs, id)) == KIND_DIR) {
            unsigned char entries[DIR_COUNT * 64];
            if (file_read(fs, id, entries, sizeof entries) < 0)
                return -1;
            for (unsigned i = 0; i < DIR_COUNT; ++i) {
                unsigned char *e = entries + i * 64;
                uint32_t child = disk_get(e);
                if (!child)
                    continue;
                if (child >= INODE_COUNT || !bit(im + MAP_I, child) || ++refs[child] != 1 ||
                    disk_get(inode(fs, child) + 8) != id || !e[4] || !memchr(e + 4, 0, 60) ||
                    strchr((char *)e + 4, '/') || !strcmp((char *)e + 4, ".") ||
                    !strcmp((char *)e + 4, ".."))
                    return -1;
                for (unsigned j = 0; j < i; ++j)
                    if (disk_get(entries + j * 64) &&
                        !strcmp((char *)entries + j * 64 + 4, (char *)e + 4))
                        return -1;
            }
        }
    for (uint32_t id = 1; id < INODE_COUNT; ++id)
        if (bit(im + MAP_I, id)) {
            if (refs[id] != 1)
                return -1;
            uint32_t p = id, steps = 0;
            while (p && steps++ < INODE_COUNT)
                p = disk_get(inode(fs, p) + 8);
            if (p)
                return -1;
        }
    return 0;
}

/* Creation ou chargement de l'image du disque en memoire. */
Sgf *sgf_open(const char *path, int create, char *error, size_t capacity) {
    Sgf *fs = calloc(1, sizeof *fs);
    int created = 0;
    if (!fs) {
        snprintf(error, capacity, "Memoire insuffisante");
        return NULL;
    }
    fs->image = calloc(1, IMAGE_SIZE);
    if (!fs->image)
        goto fail;
    /* O_EXCL empeche de remplacer un disque deja present. */
    int fd = open(path, create ? O_CREAT | O_EXCL | O_RDWR : O_RDWR, 0600);
    if (fd < 0)
        goto fail;
    created = create;
    fs->file = fdopen(fd, "r+b");
    if (!fs->file) {
        close(fd);
        goto fail;
    }
    if (create) {
        memcpy(fs->image, "MDOSv001", 8);
        uint32_t fields[] = {BLOCK_SIZE, BLOCK_COUNT, INODE_COUNT, MAP_I,
                             MAP_B, TABLE_I, DATA_START};
        for (unsigned i = 0; i < 7; ++i)
            disk_put(fs->image + 8 + i * 4, fields[i]);
        mark(fs->image + MAP_I, 0, 1);
        mark(fs->image + MAP_B, 0, 1);
        disk_put(inode(fs, 0), KIND_DIR);
        unsigned char empty[DIR_COUNT * 64] = {0};
        if (file_write(fs, 0, empty, sizeof empty) < 0 || disk_save(fs) < 0)
            goto fail;
    } else {
        if (fseek(fs->file, 0, SEEK_END) != 0 || ftell(fs->file) != IMAGE_SIZE ||
            fseek(fs->file, 0, SEEK_SET) != 0 ||
            fread(fs->image, 1, IMAGE_SIZE, fs->file) != IMAGE_SIZE || validate(fs) < 0) {
            errno = EINVAL;
            goto fail;
        }
    }
    return fs;
fail:
    {
        int saved = errno;
        snprintf(error, capacity, "Disque %s : %s", path, strerror(saved));
        sgf_close(fs);
        if (created)
            unlink(path);
        errno = saved;
        return NULL;
    }
}

/* Liberation de la memoire et fermeture du fichier de donnees. */
void sgf_close(Sgf *fs) {
    if (!fs)
        return;
    if (fs->file)
        fclose(fs->file);
    free(fs->image);
    free(fs);
}
