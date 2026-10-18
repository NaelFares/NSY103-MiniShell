/* Gestion des commandes et des fichiers du disque virtuel. */

#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

typedef struct {
    char *text;
    size_t size, used;
} Output;

/* Ajout d'un texte au resultat, dans la limite du tampon disponible. */
static int add_text(Output *o, const char *text) {
    size_t length = strlen(text);
    if (length >= o->size - o->used) {
        errno = EOVERFLOW;
        return -1;
    }
    memcpy(o->text + o->used, text, length + 1);
    o->used += length;
    return 0;
}

/* Les commandes concernent notre disque virtuel ; elles ne lancent pas de programmes Linux. */

/* On separe les arguments ; seul un > hors guillemets indique une redirection. */
static int parse(const char *line, char *buffer, char **args, int limit, int *redirect) {
    const char *read = line;
    char *write = buffer;
    int count = 0;
    *redirect = -1;
    while (*read) {
        while (*read == ' ' || *read == '\t' || *read == '\r' || *read == '\n')
            ++read;
        if (!*read)
            break;
        if (count == limit) {
            errno = E2BIG;
            return -1;
        }
        if (*read == '>') {
            if (*redirect >= 0) {
                errno = EINVAL;
                return -1;
            }
            *redirect = count;
            args[count++] = ">";
            ++read;
            continue;
        }
        args[count++] = write;
        char quote = 0;
        while (*read) {
            if (!quote && *read == '>')
                break;
            char c = *read++;
            if (!quote && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
                break;
            if (c == '\'' || c == '"') {
                if (!quote) {
                    quote = c;
                    continue;
                }
                if (quote == c) {
                    quote = 0;
                    continue;
                }
            }
            if (c == '\\' && quote != '\'') {
                if (!*read) {
                    errno = EINVAL;
                    return -1;
                }
                if (!quote || *read == '\\' || *read == '"')
                    c = *read++;
            }
            *write++ = c;
        }
        if (quote) {
            errno = EINVAL;
            return -1;
        }
        *write++ = 0;
    }
    return count;
}

/* On verifie que l'inode est un repertoire avant de lire ses entrees. */
static int directory(Sgf *fs, uint32_t id, unsigned char *data) {
    if (disk_get(inode(fs, id)) != KIND_DIR) {
        errno = ENOTDIR;
        return -1;
    }
    return file_read(fs, id, data, DIR_COUNT * 64);
}

/* On cherche un nom parmi les entrees du repertoire. */
static int find(Sgf *fs, uint32_t dir, const char *name, uint32_t *id) {
    unsigned char entries[DIR_COUNT * 64];
    if (directory(fs, dir, entries) < 0)
        return -1;
    for (unsigned i = 0; i < DIR_COUNT; ++i)
        if (disk_get(entries + i * 64) && !strcmp((char *)entries + i * 64 + 4, name)) {
            *id = disk_get(entries + i * 64);
            return 0;
        }
    errno = ENOENT;
    return -1;
}

/* On suit le chemin dossier par dossier, avec la gestion de . et .. */
static int resolve(Sgf *fs, const char *path, uint32_t *id) {
    if (!*path || strlen(path) >= COMMAND_MAX) {
        errno = EINVAL;
        return -1;
    }
    uint32_t current = *path == '/' ? 0 : fs->cwd;
    const char *p = path;
    while (*p) {
        while (*p == '/')
            ++p;
        if (!*p)
            break;
        if (disk_get(inode(fs, current)) != KIND_DIR) {
            errno = ENOTDIR;
            return -1;
        }
        const char *start = p;
        while (*p && *p != '/')
            ++p;
        size_t n = (size_t)(p - start);
        if (n > NAME_MAX_BYTES) {
            errno = ENAMETOOLONG;
            return -1;
        }
        char name[60];
        memcpy(name, start, n);
        name[n] = 0;
        if (!strcmp(name, "."))
            continue;
        if (!strcmp(name, ".."))
            current = disk_get(inode(fs, current) + 8);
        else if (find(fs, current, name, &current) < 0)
            return -1;
    }
    if (path[strlen(path) - 1] == '/' && disk_get(inode(fs, current)) != KIND_DIR) {
        errno = ENOTDIR;
        return -1;
    }
    *id = current;
    return 0;
}

/* On separe le nom final du chemin de son repertoire parent. */
static int parent_name(Sgf *fs, const char *path, uint32_t *parent, char *name) {
    size_t n = strlen(path);
    if (!n || n >= COMMAND_MAX) {
        errno = EINVAL;
        return -1;
    }
    char copy[COMMAND_MAX];
    memcpy(copy, path, n + 1);
    while (n > 1 && copy[n - 1] == '/')
        copy[--n] = 0;
    char *slash = strrchr(copy, '/'), *base = slash ? slash + 1 : copy;
    if (!*base || !strcmp(base, ".") || !strcmp(base, "..")) {
        errno = EINVAL;
        return -1;
    }
    if (strlen(base) > NAME_MAX_BYTES) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(name, base);
    if (!slash)
        *parent = fs->cwd;
    else {
        if (slash == copy)
            slash[1] = 0;
        else
            *slash = 0;
        if (resolve(fs, copy, parent) < 0)
            return -1;
    }
    if (disk_get(inode(fs, *parent)) != KIND_DIR) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

/* On ajoute le nom et le numero d'inode dans une entree libre. */
static int dir_add(Sgf *fs, uint32_t dir, const char *name, uint32_t child) {
    unsigned char entries[DIR_COUNT * 64];
    if (directory(fs, dir, entries) < 0)
        return -1;
    for (unsigned i = 0; i < DIR_COUNT; ++i)
        if (disk_get(entries + i * 64) && !strcmp((char *)entries + i * 64 + 4, name)) {
            errno = EEXIST;
            return -1;
        }
    for (unsigned i = 0; i < DIR_COUNT; ++i)
        if (!disk_get(entries + i * 64)) {
            unsigned char *e = entries + i * 64;
            memset(e, 0, 64);
            disk_put(e, child);
            strcpy((char *)e + 4, name);
            return file_write(fs, dir, entries, sizeof entries);
        }
    errno = ENOSPC;
    return -1;
}

/* On cree l'inode, puis on l'ajoute dans son repertoire parent. */
static int create_node(Sgf *fs, const char *path, uint32_t kind, uint32_t *id) {
    char name[60];
    uint32_t parent, existing;
    if (kind == KIND_FILE && *path && path[strlen(path) - 1] == '/') {
        errno = ENOTDIR;
        return -1;
    }
    if (parent_name(fs, path, &parent, name) < 0)
        return -1;
    if (find(fs, parent, name, &existing) == 0) {
        errno = EEXIST;
        return -1;
    }
    if (errno != ENOENT)
        return -1;
    *id = inode_alloc(fs, kind, parent);
    if (!*id)
        return -1;
    if (kind == KIND_DIR) {
        unsigned char empty[DIR_COUNT * 64] = {0};
        if (file_write(fs, *id, empty, sizeof empty) < 0)
            return -1;
    }
    return dir_add(fs, parent, name, *id);
}

/* On choisit le traitement selon la commande et son nombre d'arguments. */
static int execute(Sgf *fs, int argc, char **v, int redirect, Output *o, int *dirty) {
    if (!argc)
        return 0;
    const char *cmd = v[0];
    uint32_t id = 0;
    if (redirect >= 0 && strcmp(cmd, "echo")) {
        errno = EINVAL;
        return -1;
    }

    /* cd change le repertoire courant. */
    if (!strcmp(cmd, "cd") && argc <= 2) {
        if (resolve(fs, argc == 1 ? "/" : v[1], &id) < 0)
            return -1;
        if (disk_get(inode(fs, id)) != KIND_DIR) {
            errno = ENOTDIR;
            return -1;
        }
        fs->cwd = id;
        return 0;
    }

    /* df compte les blocs et les inodes utilises. */
    if (!strcmp(cmd, "df") && argc == 1) {
        unsigned blocks = 0, nodes = 0;
        for (unsigned i = 1; i < BLOCK_COUNT; ++i)
            blocks += (fs->image[MAP_B + i / 8] >> (i % 8)) & 1;
        for (unsigned i = 0; i < INODE_COUNT; ++i)
            nodes += (fs->image[MAP_I + i / 8] >> (i % 8)) & 1;
        char text[160];
        snprintf(text, sizeof text,
                 "Blocs utilises : %u/%u (512 octets)\nInodes utilises : %u/%u\n",
                 blocks, BLOCK_COUNT - 1, nodes, INODE_COUNT);
        return add_text(o, text);
    }

    /* ls -l ajoute le type, l'inode et la taille aux noms du repertoire. */
    if (!strcmp(cmd, "ls")) {
        int detailed = argc >= 2 && !strcmp(v[1], "-l");
        int path_index = detailed ? 2 : 1;
        if (argc > path_index + 1 ||
            (argc > path_index && v[path_index][0] == '-')) {
            errno = EINVAL;
            return -1;
        }
        if (resolve(fs, argc > path_index ? v[path_index] : ".", &id) < 0)
            return -1;
        unsigned char entries[DIR_COUNT * 64];
        if (directory(fs, id, entries) < 0)
            return -1;
        for (unsigned i = 0; i < DIR_COUNT; ++i) {
            uint32_t child = disk_get(entries + i * 64);
            if (!child)
                continue;
            unsigned char *node = inode(fs, child);
            char text[160];
            if (detailed) {
                snprintf(text, sizeof text, "%c inode=%u taille=%u octets ",
                         disk_get(node) == KIND_DIR ? 'd' : '-', child, disk_get(node + 4));
                if (add_text(o, text) < 0)
                    return -1;
            }
            snprintf(text, sizeof text, "%s%s\n", entries + i * 64 + 4,
                     disk_get(node) == KIND_DIR ? "/" : "");
            if (add_text(o, text) < 0)
                return -1;
        }
        return 0;
    }

    /* mkdir cree un repertoire vide. */
    if (!strcmp(cmd, "mkdir") && argc == 2) {
        *dirty = 1;
        return create_node(fs, v[1], KIND_DIR, &id);
    }

    errno = EINVAL;
    return -1;
}

/* On garde une copie du disque en memoire pour revenir en arriere en cas d'erreur. */
int sgf_execute(Sgf *fs, const char *command, char *output, size_t capacity) {
    if (!capacity) {
        errno = EINVAL;
        return -1;
    }
    output[0] = 0;
    if (strlen(command) >= COMMAND_MAX) {
        snprintf(output, capacity, "Erreur : commande trop longue\n");
        return -1;
    }
    char line[COMMAND_MAX], *args[8];
    int redirect;
    int argc = parse(command, line, args, 8, &redirect);
    if (argc < 0) {
        snprintf(output, capacity, "Erreur : syntaxe invalide (guillemets ou arguments)\n");
        return -1;
    }
    unsigned char *backup = malloc(IMAGE_SIZE);
    if (!backup) {
        snprintf(output, capacity, "Erreur : memoire insuffisante\n");
        return -1;
    }
    memcpy(backup, fs->image, IMAGE_SIZE);
    uint32_t cwd = fs->cwd;
    Output o = {output, capacity, 0};
    int dirty = 0;
    int result = execute(fs, argc, args, redirect, &o, &dirty);

    /* On sauvegarde seulement si la commande a modifie le disque. */
    if (result == 0 && dirty)
        result = disk_save(fs);
    if (result < 0) {

        /* En cas d'erreur, on remet le disque et le repertoire courant comme avant. */
        int saved = errno;
        memcpy(fs->image, backup, IMAGE_SIZE);
        fs->cwd = cwd;
        snprintf(output, capacity, "Erreur : %s\n", strerror(saved));
        errno = saved;
    }
    free(backup);
    return result;
}
