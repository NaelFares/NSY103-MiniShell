#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

uint32_t disk_get(const unsigned char *p) {
    uint32_t v=0; for(unsigned i=0;i<4;++i) v|=(uint32_t)p[i]<<(8*i); return v;
}
void disk_put(unsigned char *p,uint32_t v) {
    for(unsigned i=0;i<4;++i) p[i]=(unsigned char)(v>>(8*i));
}
static int bit(const unsigned char *m,uint32_t id) { return (m[id/8]>>(id%8))&1; }
static void mark(unsigned char *m,uint32_t id,int used) {
    unsigned char mask=(unsigned char)(1u<<(id%8));
    if(used) m[id/8]|=mask; else m[id/8]&=(unsigned char)~mask;
}
unsigned char *inode(Sgf *fs,uint32_t id) { return fs->image+TABLE_I+id*INODE_SIZE; }
static unsigned char *block(Sgf *fs,uint32_t id) {
    return fs->image+DATA_START+id*BLOCK_SIZE;
}
/* Block zero is a reserved sentinel, never allocated. */
static uint32_t block_alloc(Sgf *fs) {
    for(uint32_t i=1;i<BLOCK_COUNT;++i) if(!bit(fs->image+MAP_B,i)) {
        mark(fs->image+MAP_B,i,1); memset(block(fs,i),0,BLOCK_SIZE); return i;
    }
    errno=ENOSPC; return 0;
}
uint32_t inode_alloc(Sgf *fs,uint32_t kind,uint32_t parent) {
    for(uint32_t i=1;i<INODE_COUNT;++i) if(!bit(fs->image+MAP_I,i)) {
        mark(fs->image+MAP_I,i,1); memset(inode(fs,i),0,INODE_SIZE);
        disk_put(inode(fs,i),kind); disk_put(inode(fs,i)+8,parent); return i;
    }
    errno=ENOSPC; return 0;
}
static uint32_t data_block(Sgf *fs,unsigned char *node,uint32_t index) {
    if(index<DIRECT_COUNT) return disk_get(node+12+index*4);
    uint32_t indirect=disk_get(node+44);
    return indirect ? disk_get(block(fs,indirect)+(index-DIRECT_COUNT)*4) : 0;
}
static void release_data(Sgf *fs,unsigned char *node) {
    uint32_t count=(disk_get(node+4)+BLOCK_SIZE-1)/BLOCK_SIZE;
    for(uint32_t i=0;i<count;++i) mark(fs->image+MAP_B,data_block(fs,node,i),0);
    uint32_t indirect=disk_get(node+44);
    if(indirect) mark(fs->image+MAP_B,indirect,0);
    memset(node+12,0,36); disk_put(node+4,0);
}
void inode_free(Sgf *fs,uint32_t id) {
    release_data(fs,inode(fs,id)); memset(inode(fs,id),0,INODE_SIZE);
    mark(fs->image+MAP_I,id,0);
}
int file_read(Sgf *fs,uint32_t id,unsigned char *out,size_t capacity) {
    unsigned char *node=inode(fs,id); size_t size=disk_get(node+4);
    if(size>capacity) { errno=EFBIG; return -1; }
    for(size_t pos=0;pos<size;pos+=BLOCK_SIZE) {
        size_t n=size-pos; if(n>BLOCK_SIZE) n=BLOCK_SIZE;
        memcpy(out+pos,block(fs,data_block(fs,node,(uint32_t)(pos/BLOCK_SIZE))),n);
    }
    return 0;
}
int file_write(Sgf *fs,uint32_t id,const unsigned char *data,size_t size) {
    if(size>FILE_MAX) { errno=EFBIG; return -1; }
    unsigned char *node=inode(fs,id); release_data(fs,node);
    if(size>DIRECT_COUNT*BLOCK_SIZE) {
        uint32_t b=block_alloc(fs); if(!b) return -1; disk_put(node+44,b);
    }
    for(size_t pos=0;pos<size;pos+=BLOCK_SIZE) {
        uint32_t b=block_alloc(fs); if(!b) return -1;
        uint32_t index=(uint32_t)(pos/BLOCK_SIZE);
        if(index<DIRECT_COUNT) disk_put(node+12+index*4,b);
        else disk_put(block(fs,disk_get(node+44))+(index-DIRECT_COUNT)*4,b);
        size_t n=size-pos; if(n>BLOCK_SIZE) n=BLOCK_SIZE;
        memcpy(block(fs,b),data+pos,n);
        disk_put(node+4,(uint32_t)(pos+n));
    }
    return 0;
}
static int write_image(int fd,const unsigned char *image) {
    size_t done=0;
    while(done<IMAGE_SIZE) {
        ssize_t n=write(fd,image+done,IMAGE_SIZE-done);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return -1;
        done+=(size_t)n;
    }
    return fsync(fd);
}
int disk_save(Sgf *fs) {
    size_t n=strlen(fs->path)+32; char *temp=malloc(n);
    if(!temp) return -1;
    snprintf(temp,n,"%s.tmp.XXXXXX",fs->path);
    int fd=mkstemp(temp); int result=-1;
    if(fd>=0) {
        result=write_image(fd,fs->image);
        int saved=errno;
        if(close(fd)<0 && result==0) { result=-1; saved=errno; }
        if(result==0) { result=rename(temp,fs->path); saved=errno; }
        if(result<0) unlink(temp);
        errno=saved;
    }
    free(temp); return result;
}
/* Reject corrupt images before any pointer is used by file operations. */
static int validate(Sgf *fs) {
    const unsigned char *im=fs->image;
    if(memcmp(im,"MDOSv001",8) || disk_get(im+8)!=BLOCK_SIZE ||
       disk_get(im+12)!=BLOCK_COUNT || disk_get(im+16)!=INODE_COUNT ||
       disk_get(im+20)!=MAP_I || disk_get(im+24)!=MAP_B ||
       disk_get(im+28)!=TABLE_I || disk_get(im+32)!=DATA_START) return -1;
    if(!bit(im+MAP_I,0) || !bit(im+MAP_B,0) || disk_get(inode(fs,0))!=KIND_DIR ||
       disk_get(inode(fs,0)+8)!=0) return -1;
    unsigned char seen[BLOCK_COUNT]={0}; seen[0]=1;
    for(uint32_t id=0;id<INODE_COUNT;++id) if(bit(im+MAP_I,id)) {
        unsigned char *node=inode(fs,id); uint32_t kind=disk_get(node),size=disk_get(node+4);
        uint32_t parent=disk_get(node+8);
        if((kind!=KIND_FILE && kind!=KIND_DIR) || size>FILE_MAX ||
           (kind==KIND_DIR && size!=DIR_COUNT*64) || parent>=INODE_COUNT ||
           !bit(im+MAP_I,parent) || disk_get(inode(fs,parent))!=KIND_DIR) return -1;
        uint32_t indirect=disk_get(node+44),count=(size+BLOCK_SIZE-1)/BLOCK_SIZE;
        if(count>DIRECT_COUNT) {
            if(!indirect || indirect>=BLOCK_COUNT || seen[indirect] || !bit(im+MAP_B,indirect)) return -1;
            seen[indirect]=1;
        } else if(indirect) return -1;
        for(uint32_t i=0;i<FILE_BLOCKS;++i) {
            if(i>=DIRECT_COUNT && !indirect) break;
            uint32_t b=data_block(fs,node,i);
            if(i>=count) { if(b) return -1; continue; }
            if(!b || b>=BLOCK_COUNT || seen[b] || !bit(im+MAP_B,b)) return -1;
            seen[b]=1;
        }
    }
    for(uint32_t i=0;i<BLOCK_COUNT;++i) if(bit(im+MAP_B,i)!=seen[i]) return -1;
    /* Check directory ownership, names, references and reachability. */
    unsigned refs[INODE_COUNT]={0};
    for(uint32_t id=0;id<INODE_COUNT;++id) if(bit(im+MAP_I,id) && disk_get(inode(fs,id))==KIND_DIR) {
        unsigned char entries[DIR_COUNT*64];
        if(file_read(fs,id,entries,sizeof entries)<0) return -1;
        for(unsigned i=0;i<DIR_COUNT;++i) {
            unsigned char *e=entries+i*64; uint32_t child=disk_get(e);
            if(!child) continue;
            if(child>=INODE_COUNT || !bit(im+MAP_I,child) || ++refs[child]!=1 ||
               disk_get(inode(fs,child)+8)!=id || !e[4] || !memchr(e+4,0,60) ||
               strchr((char *)e+4,'/') || !strcmp((char *)e+4,".") || !strcmp((char *)e+4,"..")) return -1;
            for(unsigned j=0;j<i;++j) if(disk_get(entries+j*64) &&
                !strcmp((char *)entries+j*64+4,(char *)e+4)) return -1;
        }
    }
    for(uint32_t id=1;id<INODE_COUNT;++id) if(bit(im+MAP_I,id)) {
        if(refs[id]!=1) return -1;
        uint32_t p=id,steps=0;
        while(p && steps++<INODE_COUNT) p=disk_get(inode(fs,p)+8);
        if(p) return -1;
    }
    return 0;
}
Sgf *sgf_open(const char *path,int create,char *error,size_t capacity) {
    Sgf *fs=calloc(1,sizeof *fs); int fd=-1,created=0;
    if(!fs) { snprintf(error,capacity,"Memoire insuffisante"); return NULL; }
    fs->lock_fd=-1; fs->path=strdup(path); fs->image=calloc(1,IMAGE_SIZE);
    char *lock=malloc(strlen(path)+6);
    if(!fs->path || !fs->image || !lock) { free(lock); goto fail; }
    sprintf(lock,"%s.lock",path);
    fs->lock_fd=open(lock,O_CREAT|O_RDWR|O_NOFOLLOW,0600); free(lock);
    if(fs->lock_fd<0 || flock(fs->lock_fd,LOCK_EX|LOCK_NB)<0) goto fail;
    fd=open(path,create ? O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW : O_RDONLY|O_NOFOLLOW,0600);
    if(fd<0) goto fail;
    if(create) {
        created=1; memcpy(fs->image,"MDOSv001",8);
        uint32_t fields[]={BLOCK_SIZE,BLOCK_COUNT,INODE_COUNT,MAP_I,MAP_B,TABLE_I,DATA_START};
        for(unsigned i=0;i<7;++i) disk_put(fs->image+8+i*4,fields[i]);
        mark(fs->image+MAP_I,0,1); mark(fs->image+MAP_B,0,1);
        disk_put(inode(fs,0),KIND_DIR);
        unsigned char empty[DIR_COUNT*64]={0};
        if(file_write(fs,0,empty,sizeof empty)<0 || write_image(fd,fs->image)<0) goto fail;
    } else {
        struct stat st;
        if(fstat(fd,&st)<0) goto fail;
        if(!S_ISREG(st.st_mode) || st.st_size!=IMAGE_SIZE) { errno=EINVAL; goto fail; }
        size_t done=0;
        while(done<IMAGE_SIZE) {
            ssize_t n=read(fd,fs->image+done,IMAGE_SIZE-done);
            if(n<0 && errno==EINTR) continue;
            if(n<=0) { errno=EINVAL; goto fail; } done+=(size_t)n;
        }
        if(validate(fs)<0) { errno=EINVAL; goto fail; }
    }
    if(close(fd)<0) { fd=-1; goto fail; }
    return fs;
fail: {
    int saved=errno; if(fd>=0) close(fd); if(created) unlink(path);
    snprintf(error,capacity,"Disque %s : %s",path,strerror(saved));
    sgf_close(fs); errno=saved; return NULL;
    }
}
void sgf_close(Sgf *fs) {
    if(!fs) return;
    if(fs->lock_fd>=0) close(fs->lock_fd);
    free(fs->image); free(fs->path); free(fs);
}
