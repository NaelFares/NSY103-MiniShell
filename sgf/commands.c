/* Gestion des commandes et des fichiers du disque virtuel. */

#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

/* Reconstruction du chemin courant en remontant les repertoires parents. */
static int print_path(Sgf *fs, Output *o) {
    char path[COMMAND_MAX];
    size_t pos = sizeof path - 1;
    path[pos] = 0;
    uint32_t current = fs->cwd;
    unsigned depth = 0;
    while (current != 0) {
        if (++depth >= INODE_COUNT) {
            errno = EINVAL;
            return -1;
        }
        uint32_t parent = disk_get(inode(fs, current) + 8);
        unsigned char entries[DIR_COUNT * 64];
        if (directory(fs, parent, entries) < 0)
            return -1;
        unsigned i;
        for (i = 0; i < DIR_COUNT; ++i)
            if (disk_get(entries + i * 64) == current)
                break;
        if (i == DIR_COUNT) {
            errno = ENOENT;
            return -1;
        }
        const char *name = (char *)entries + i * 64 + 4;
        size_t length = strlen(name);
        if (length + 1 > pos) {
            errno = ENAMETOOLONG;
            return -1;
        }
        pos -= length;
        memcpy(path + pos, name, length);
        path[--pos] = '/';
        current = parent;
    }
    if (add_text(o, fs->cwd == 0 ? "/" : path + pos) < 0)
        return -1;
    return add_text(o, "\n");
}

/* Conversion d'une liste de lettres r, w, x en droits. */
static int parse_rights(const char *text, size_t length) {
    unsigned rights = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned bit;
        if (text[i] == 'r') bit = ACCESS_READ;
        else if (text[i] == 'w') bit = ACCESS_WRITE;
        else if (text[i] == 'x') bit = ACCESS_EXEC;
        else { errno = EINVAL; return -1; }
        if (rights & bit) { errno = EINVAL; return -1; }
        rights |= bit;
    }
    return (int)rights;
}

/* Affichage de deux categories de trois droits : utilisateur puis autres. */
static void rights_text(const unsigned char *node, char text[8]) {
    unsigned mode = disk_get(node + INODE_RIGHTS);
    text[0] = disk_get(node) == KIND_DIR ? 'd' : '-';
    for (unsigned i = 0; i < 6; ++i)
        text[i + 1] = (mode & (1u << (5 - i))) ? "rwx"[i % 3] : '-';
    text[7] = 0;
}

/* Conversion de la date stockee dans l'inode vers l'heure locale. */
static void modification_date(const unsigned char *node, char text[32]) {
    time_t stamp = (time_t)disk_get(node + INODE_MTIME);
    struct tm local;
    if (!stamp || !localtime_r(&stamp, &local) ||
        !strftime(text, 32, "%d/%m/%Y %H:%M", &local))
        strcpy(text, "inconnue");
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

    /* chmod ne gere que l'utilisateur proprietaire et les autres. */
    if (!strcmp(cmd, "chmod") && argc == 3) {
        const char *separator = strstr(v[1], ",o=");
        if (strncmp(v[1], "u=", 2) || !separator) { errno = EINVAL; return -1; }
        int user = parse_rights(v[1] + 2, (size_t)(separator - (v[1] + 2)));
        int others = parse_rights(separator + 3, strlen(separator + 3));
        if (user < 0 || others < 0) return -1;
        *dirty = 1;
        return sgf_chmod(fs, v[2], (unsigned)user, (unsigned)others);
    }

    /* pwd affiche le chemin absolu du repertoire courant du disque virtuel. */
    if (!strcmp(cmd, "pwd") && argc == 1)
        return print_path(fs, o);

    /* cd change le repertoire courant. */
    if (!strcmp(cmd, "cd") && argc <= 2) {
        if (resolve(fs, argc == 1 ? "/" : v[1], &id) < 0)
            return -1;
        if (disk_get(inode(fs, id)) != KIND_DIR) {
            errno = ENOTDIR;
            return -1;
        }
        if (inode_access(fs, id, ACCESS_EXEC) < 0)
            return -1;
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

    /* ls affiche les noms ; l'option -l ajoute les informations des inodes. */
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
        if (inode_access(fs, id, ACCESS_READ | ACCESS_EXEC) < 0)
            return -1;
        if (directory(fs, id, entries) < 0)
            return -1;
        char text[200];
        if (detailed) {
            snprintf(text, sizeof text, "%-7s %-6s %-5s %9s %-16s %s\n",
                     "Droits", "UID", "Inode", "Taille(o)", "Date/heure", "Nom");
            if (add_text(o, text) < 0) return -1;
        }
        for (unsigned i = 0; i < DIR_COUNT; ++i) {
            uint32_t child = disk_get(entries + i * 64);
            if (!child)
                continue;
            unsigned char *node = inode(fs, child);
            const char *name = (char *)entries + i * 64 + 4;
            const char *suffix = disk_get(node) == KIND_DIR ? "/" : "";
            if (detailed) {
                char rights[8], date[32];
                rights_text(node, rights);
                modification_date(node, date);
                snprintf(text, sizeof text, "%-7s %-6u %-5u %9u %-16s %s%s\n",
                         rights, disk_get(node + INODE_OWNER), child, disk_get(node + 4),
                         date, name, suffix);
            } else {
                snprintf(text, sizeof text, "%s%s\n", name, suffix);
            }
            if (add_text(o, text) < 0) return -1;
        }
        return 0;
    }

    /* mkdir cree un repertoire vide. */
    if (!strcmp(cmd, "mkdir") && argc == 2) {
        *dirty = 1;
        return _mkdir(fs, v[1]);
    }

    /* cat ouvre le fichier, lit son contenu, puis le ferme. */
    if (!strcmp(cmd, "cat") && argc == 2) {
        int file = _myopen(fs, v[1], SGF_READ);
        if (file < 0) return -1;
        unsigned char data[FILE_MAX + 1];
        int length = _myread(fs, file, data, FILE_MAX);
        int saved = errno;
        _myclose(fs, file);
        if (length < 0) { errno = saved; return -1; }
        if (memchr(data, 0, (size_t)length)) { errno = EILSEQ; return -1; }
        data[length] = 0;
        return add_text(o, (char *)data);
    }

    /* echo cree ou vide le fichier avant d'y ecrire son texte. */
    if (!strcmp(cmd, "echo") && argc == 4 && redirect == 2) {
        *dirty = 1;
        unsigned char data[COMMAND_MAX];
        size_t length = strlen(v[1]);
        if (length + 1 > sizeof data || length + 1 > FILE_MAX) {
            errno = EFBIG;
            return -1;
        }
        memcpy(data, v[1], length);
        data[length] = '\n';
        int file = _mycreat(fs, v[3], SGF_WRITE);
        if (file < 0) return -1;
        int result = _mywrite(fs, file, data, length + 1);
        int saved = errno;
        _myclose(fs, file);
        if (result < 0) { errno = saved; return -1; }
        return 0;
    }

    /* Les primitives du SGF controlent le type et le contenu avant suppression. */
    if (!strcmp(cmd, "rm") && argc == 2) {
        *dirty = 1;
        return _unlink(fs, v[1]);
    }
    if (!strcmp(cmd, "rmdir") && argc == 2) {
        *dirty = 1;
        return _rmdir(fs, v[1]);
    }

    /* cp et mv refusent d'ecraser un nom deja present. */
    if ((!strcmp(cmd, "cp") || !strcmp(cmd, "mv")) && argc == 3) {
        *dirty = 1;
        char oldname[60], name[60];
        uint32_t parent, oldparent, other;
        if (resolve(fs, v[1], &id) < 0 || parent_name(fs, v[1], &oldparent, oldname) < 0 ||
            parent_name(fs, v[2], &parent, name) < 0)
            return -1;
        if (inode_access(fs, parent, ACCESS_WRITE | ACCESS_EXEC) < 0 ||
            (!strcmp(cmd, "mv") && inode_access(fs, oldparent, ACCESS_WRITE | ACCESS_EXEC) < 0))
            return -1;
        if (find(fs, parent, name, &other) == 0) {
            errno = EEXIST;
            return -1;
        }
        if (errno != ENOENT)
            return -1;
        if (!strcmp(cmd, "cp")) {
            int source = _myopen(fs, v[1], SGF_READ);
            if (source < 0) return -1;
            unsigned char data[FILE_MAX];
            int length = _myread(fs, source, data, sizeof data);
            int saved = errno;
            _myclose(fs, source);
            if (length < 0) { errno = saved; return -1; }
            int destination = _mycreat(fs, v[2], SGF_WRITE);
            if (destination < 0) return -1;
            int result = _mywrite(fs, destination, data, (size_t)length);
            saved = errno;
            _myclose(fs, destination);
            if (result < 0) { errno = saved; return -1; }
            return 0;
        }
        uint32_t p = parent;
        for (;;) {
            if (p == id) {
                errno = EINVAL;
                return -1;
            }
            if (!p)
                break;
            p = disk_get(inode(fs, p) + 8);
        }
        if (dir_remove(fs, oldparent, oldname) < 0 || dir_add(fs, parent, name, id) < 0)
            return -1;
        disk_put(inode(fs, id) + 8, parent);
        return 0;
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
    int modes[INODE_COUNT];
    size_t positions[INODE_COUNT];
    memcpy(modes, fs->open_mode, sizeof modes);
    memcpy(positions, fs->position, sizeof positions);
    Output o = {output, capacity, 0};
    int dirty = 0;
    fs->in_command = 1;
    int result = execute(fs, argc, args, redirect, &o, &dirty);

    /* On sauvegarde seulement si la commande a modifie le disque. */
    if (result == 0 && dirty)
        result = disk_save(fs);
    fs->in_command = 0;
    if (result < 0) {

        /* En cas d'erreur, on remet le disque et le repertoire courant comme avant. */
        int saved = errno;
        memcpy(fs->image, backup, IMAGE_SIZE);
        fs->cwd = cwd;
        memcpy(fs->open_mode, modes, sizeof modes);
        memcpy(fs->position, positions, sizeof positions);
        snprintf(output, capacity, "Erreur : %s\n", strerror(saved));
        errno = saved;
    }
    free(backup);
    return result;
}
