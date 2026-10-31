/* Primitives du SGF pour les fichiers et les repertoires. */

#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "protocol.h"
#include <errno.h>
#include <string.h>

/* Selection des droits du proprietaire ou de la categorie les autres. */
int inode_access(Sgf *fs, uint32_t id, unsigned rights) {
    unsigned char *node = inode(fs, id);
    unsigned mode = disk_get(node + INODE_RIGHTS);
    unsigned allowed = disk_get(node + INODE_OWNER) == fs->user_id ? (mode >> 3) : mode;
    if ((allowed & rights) != rights) { errno = EACCES; return -1; }
    return 0;
}

/* On verifie que l'inode est un repertoire avant de lire ses entrees. */
int directory(Sgf *fs, uint32_t id, unsigned char *data) {
    if (disk_get(inode(fs, id)) != KIND_DIR) {
        errno = ENOTDIR;
        return -1;
    }
    return file_read(fs, id, data, DIR_COUNT * 64);
}

/* On cherche un nom parmi les entrees du repertoire. */
int find(Sgf *fs, uint32_t dir, const char *name, uint32_t *id) {
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
int resolve(Sgf *fs, const char *path, uint32_t *id) {
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
        if (inode_access(fs, current, ACCESS_EXEC) < 0)
            return -1;
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
int parent_name(Sgf *fs, const char *path, uint32_t *parent, char *name) {
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
int dir_add(Sgf *fs, uint32_t dir, const char *name, uint32_t child) {
    if (inode_access(fs, dir, ACCESS_WRITE | ACCESS_EXEC) < 0) return -1;
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

/* On retire le nom du repertoire, sans liberer ici les donnees du fichier. */
int dir_remove(Sgf *fs, uint32_t dir, const char *name) {
    if (inode_access(fs, dir, ACCESS_WRITE | ACCESS_EXEC) < 0) return -1;
    unsigned char entries[DIR_COUNT * 64];
    if (directory(fs, dir, entries) < 0)
        return -1;
    for (unsigned i = 0; i < DIR_COUNT; ++i)
        if (disk_get(entries + i * 64) && !strcmp((char *)entries + i * 64 + 4, name)) {
            memset(entries + i * 64, 0, 64);
            return file_write(fs, dir, entries, sizeof entries);
        }
    errno = ENOENT;
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
    if (inode_access(fs, parent, ACCESS_WRITE | ACCESS_EXEC) < 0)
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
        if (file_write(fs, *id, empty, sizeof empty) < 0) {
            inode_free(fs, *id);
            return -1;
        }
    }
    if (dir_add(fs, parent, name, *id) < 0) {
        inode_free(fs, *id);
        return -1;
    }
    return 0;
}


/* Verification du mode d'ouverture demande. */
static int valid_mode(int mode) {
    return mode == SGF_READ || mode == SGF_WRITE || mode == (SGF_READ | SGF_WRITE);
}

/* Ouverture d'un fichier ; le mode ecriture autorise sa creation. */
int _myopen(Sgf *fs, const char *name, int mode) {
    if (!valid_mode(mode)) { errno = EINVAL; return -1; }
    uint32_t id;
    if (resolve(fs, name, &id) < 0) {
        if (errno != ENOENT || !(mode & SGF_WRITE) ||
            create_node(fs, name, KIND_FILE, &id) < 0)
            return -1;
    }
    if (disk_get(inode(fs, id)) != KIND_FILE) { errno = EISDIR; return -1; }
    unsigned rights = 0;
    if (mode & SGF_READ) rights |= ACCESS_READ;
    if (mode & SGF_WRITE) rights |= ACCESS_WRITE;
    if (inode_access(fs, id, rights) < 0) return -1;
    if (fs->open_mode[id]) { errno = EBUSY; return -1; }
    fs->open_mode[id] = mode;
    fs->position[id] = 0;
    return (int)id;
}

/* Creation d'un fichier vide ; un fichier existant est remis a zero. */
int _mycreat(Sgf *fs, const char *name, int mode) {
    if (!valid_mode(mode) || !(mode & SGF_WRITE)) { errno = EINVAL; return -1; }
    int id = _myopen(fs, name, mode);
    if (id < 0) return -1;
    if (file_write(fs, (uint32_t)id, NULL, 0) < 0) {
        fs->open_mode[id] = 0;
        return -1;
    }
    return id;
}

/* Verification d'un inode ouvert, avant une lecture ou une ecriture. */
static int opened(Sgf *fs, int id, int mode) {
    if (id <= 0 || id >= (int)INODE_COUNT || !fs->open_mode[id]) {
        errno = EBADF;
        return -1;
    }
    if (mode && !(fs->open_mode[id] & mode)) { errno = EACCES; return -1; }
    return 0;
}

/* Fermeture du fichier et liberation de sa position de lecture/ecriture. */
int _myclose(Sgf *fs, int id) {
    if (opened(fs, id, 0) < 0) return -1;
    if ((fs->open_mode[id] & SGF_WRITE) && !fs->in_command && disk_save(fs) < 0)
        return -1;
    fs->open_mode[id] = 0;
    fs->position[id] = 0;
    return 0;
}

/* Lecture d'au plus count octets, a partir de la position courante. */
int _myread(Sgf *fs, int id, void *buffer, size_t count) {
    if (opened(fs, id, SGF_READ) < 0) return -1;
    if (count && !buffer) { errno = EINVAL; return -1; }
    size_t size = disk_get(inode(fs, (uint32_t)id) + 4);
    size_t pos = fs->position[id];
    if (pos >= size || count == 0) return 0;
    if (count > size - pos) count = size - pos;
    unsigned char data[FILE_MAX];
    if (file_read(fs, (uint32_t)id, data, sizeof data) < 0) return -1;
    memcpy(buffer, data + pos, count);
    fs->position[id] += count;
    return (int)count;
}

/* Ecriture de count octets, sans effacer le contenu situe apres. */
int _mywrite(Sgf *fs, int id, const void *buffer, size_t count) {
    if (opened(fs, id, SGF_WRITE) < 0) return -1;
    if (count && !buffer) { errno = EINVAL; return -1; }
    if (count == 0) return 0;
    size_t pos = fs->position[id];
    if (pos > FILE_MAX || count > FILE_MAX - pos) { errno = EFBIG; return -1; }
    unsigned char data[FILE_MAX];
    if (file_read(fs, (uint32_t)id, data, sizeof data) < 0) return -1;
    size_t size = disk_get(inode(fs, (uint32_t)id) + 4);
    memcpy(data + pos, buffer, count);
    if (pos + count > size) size = pos + count;
    if (file_write(fs, (uint32_t)id, data, size) < 0) return -1;
    fs->position[id] += count;
    return (int)count;
}

/* Creation d'un repertoire vide dans son parent. */
int _mkdir(Sgf *fs, const char *name) {
    uint32_t id;
    if (create_node(fs, name, KIND_DIR, &id) < 0) return -1;
    return fs->in_command ? 0 : disk_save(fs);
}

/* Suppression d'un repertoire vide qui n'est pas utilise comme chemin courant. */
int _rmdir(Sgf *fs, const char *path) {
    uint32_t id, parent;
    char name[60];
    if (resolve(fs, path, &id) < 0 || parent_name(fs, path, &parent, name) < 0)
        return -1;
    unsigned char entries[DIR_COUNT * 64];
    if (directory(fs, id, entries) < 0) return -1;
    for (unsigned i = 0; i < DIR_COUNT; ++i)
        if (disk_get(entries + i * 64)) { errno = ENOTEMPTY; return -1; }
    uint32_t current = fs->cwd;
    for (;;) {
        if (current == id) { errno = EBUSY; return -1; }
        if (current == 0) break;
        current = disk_get(inode(fs, current) + 8);
    }
    if (dir_remove(fs, parent, name) < 0) return -1;
    inode_free(fs, id);
    return fs->in_command ? 0 : disk_save(fs);
}

/* Suppression de l'entree d'un fichier et liberation de son inode et de ses blocs. */
int _unlink(Sgf *fs, const char *path) {
    uint32_t id, parent;
    char name[60];
    if (resolve(fs, path, &id) < 0 || parent_name(fs, path, &parent, name) < 0)
        return -1;
    if (disk_get(inode(fs, id)) != KIND_FILE) { errno = EISDIR; return -1; }
    if (fs->open_mode[id]) { errno = EBUSY; return -1; }
    if (dir_remove(fs, parent, name) < 0) return -1;
    inode_free(fs, id);
    return fs->in_command ? 0 : disk_save(fs);
}

/* Modification des deux categories de droits, sans notion de groupe. */
int sgf_chmod(Sgf *fs, const char *name, unsigned user_rights, unsigned other_rights) {
    if (user_rights > 7 || other_rights > 7) { errno = EINVAL; return -1; }
    uint32_t id;
    if (resolve(fs, name, &id) < 0) return -1;
    if (disk_get(inode(fs, id) + INODE_OWNER) != fs->user_id) {
        errno = EPERM;
        return -1;
    }
    disk_put(inode(fs, id) + INODE_RIGHTS, (user_rights << 3) | other_rights);
    return fs->in_command ? 0 : disk_save(fs);
}
