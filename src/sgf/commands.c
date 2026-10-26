#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

typedef struct { char *text; size_t size,used; } Output;
static int emit(Output *o,const char *format,...) {
    va_list ap; va_start(ap,format);
    int n=vsnprintf(o->text+o->used,o->size-o->used,format,ap); va_end(ap);
    if(n<0 || (size_t)n>=o->size-o->used) { errno=EOVERFLOW; return -1; }
    o->used+=(size_t)n; return 0;
}
/* Small command language: quotes, escaped characters, no host execution. */
static int parse(char *line,char **args,int limit) {
    char *read=line,*write=line; int count=0;
    while(*read) {
        while(*read==' ' || *read=='\t' || *read=='\r' || *read=='\n') ++read;
        if(!*read) break;
        if(count==limit) { errno=E2BIG; return -1; }
        args[count++]=write; char quote=0;
        while(*read) {
            char c=*read++;
            if(!quote && (c==' ' || c=='\t' || c=='\r' || c=='\n')) break;
            if(c=='\'' || c=='"') {
                if(!quote) { quote=c; continue; }
                if(quote==c) { quote=0; continue; }
            }
            if(c=='\\' && quote!='\'') {
                if(!*read) { errno=EINVAL; return -1; }
                c=*read++; if(c=='n') c='\n'; else if(c=='t') c='\t';
            }
            *write++=c;
        }
        if(quote) { errno=EINVAL; return -1; }
        *write++=0;
    }
    return count;
}
static int directory(Sgf *fs,uint32_t id,unsigned char *data) {
    if(disk_get(inode(fs,id))!=KIND_DIR) { errno=ENOTDIR; return -1; }
    return file_read(fs,id,data,DIR_COUNT*64);
}
static int find(Sgf *fs,uint32_t dir,const char *name,uint32_t *id) {
    unsigned char entries[DIR_COUNT*64];
    if(directory(fs,dir,entries)<0) return -1;
    for(unsigned i=0;i<DIR_COUNT;++i) if(disk_get(entries+i*64) &&
        !strcmp((char *)entries+i*64+4,name)) { *id=disk_get(entries+i*64); return 0; }
    errno=ENOENT; return -1;
}
static int resolve(Sgf *fs,const char *path,uint32_t *id) {
    if(!*path || strlen(path)>=COMMAND_MAX) { errno=EINVAL; return -1; }
    uint32_t current=*path=='/' ? 0 : fs->cwd;
    const char *p=path;
    while(*p) {
        while(*p=='/') ++p;
        if(!*p) break;
        if(disk_get(inode(fs,current))!=KIND_DIR) { errno=ENOTDIR; return -1; }
        const char *start=p; while(*p && *p!='/') ++p;
        size_t n=(size_t)(p-start);
        if(n>NAME_MAX_BYTES) { errno=ENAMETOOLONG; return -1; }
        char name[60]; memcpy(name,start,n); name[n]=0;
        if(!strcmp(name,".")) continue;
        if(!strcmp(name,"..")) current=disk_get(inode(fs,current)+8);
        else if(find(fs,current,name,&current)<0) return -1;
    }
    if(path[strlen(path)-1]=='/' && disk_get(inode(fs,current))!=KIND_DIR) {
        errno=ENOTDIR; return -1;
    }
    *id=current; return 0;
}
static int parent_name(Sgf *fs,const char *path,uint32_t *parent,char *name) {
    size_t n=strlen(path);
    if(!n || n>=COMMAND_MAX) { errno=EINVAL; return -1; }
    char copy[COMMAND_MAX]; memcpy(copy,path,n+1);
    while(n>1 && copy[n-1]=='/') copy[--n]=0;
    char *slash=strrchr(copy,'/'),*base=slash ? slash+1 : copy;
    if(!*base || !strcmp(base,".") || !strcmp(base,"..")) { errno=EINVAL; return -1; }
    if(strlen(base)>NAME_MAX_BYTES) { errno=ENAMETOOLONG; return -1; }
    strcpy(name,base);
    if(!slash) *parent=fs->cwd;
    else {
        if(slash==copy) slash[1]=0; else *slash=0;
        if(resolve(fs,copy,parent)<0) return -1;
    }
    if(disk_get(inode(fs,*parent))!=KIND_DIR) { errno=ENOTDIR; return -1; }
    return 0;
}
static int dir_add(Sgf *fs,uint32_t dir,const char *name,uint32_t child) {
    unsigned char entries[DIR_COUNT*64]; if(directory(fs,dir,entries)<0) return -1;
    for(unsigned i=0;i<DIR_COUNT;++i) if(disk_get(entries+i*64) &&
        !strcmp((char *)entries+i*64+4,name)) { errno=EEXIST; return -1; }
    for(unsigned i=0;i<DIR_COUNT;++i) if(!disk_get(entries+i*64)) {
        unsigned char *e=entries+i*64; memset(e,0,64); disk_put(e,child); strcpy((char *)e+4,name);
        return file_write(fs,dir,entries,sizeof entries);
    }
    errno=ENOSPC; return -1;
}
static int dir_remove(Sgf *fs,uint32_t dir,const char *name) {
    unsigned char entries[DIR_COUNT*64]; if(directory(fs,dir,entries)<0) return -1;
    for(unsigned i=0;i<DIR_COUNT;++i) if(disk_get(entries+i*64) &&
        !strcmp((char *)entries+i*64+4,name)) {
        memset(entries+i*64,0,64); return file_write(fs,dir,entries,sizeof entries);
    }
    errno=ENOENT; return -1;
}
static int create_node(Sgf *fs,const char *path,uint32_t kind,uint32_t *id) {
    char name[60]; uint32_t parent,existing;
    if(kind==KIND_FILE && *path && path[strlen(path)-1]=='/') { errno=ENOTDIR; return -1; }
    if(parent_name(fs,path,&parent,name)<0) return -1;
    if(find(fs,parent,name,&existing)==0) { errno=EEXIST; return -1; }
    if(errno!=ENOENT) return -1;
    *id=inode_alloc(fs,kind,parent); if(!*id) return -1;
    if(kind==KIND_DIR) {
        unsigned char empty[DIR_COUNT*64]={0};
        if(file_write(fs,*id,empty,sizeof empty)<0) return -1;
    }
    return dir_add(fs,parent,name,*id);
}
static int print_path(Sgf *fs,Output *o) {
    char path[INODE_COUNT*61]; size_t pos=sizeof path-1; path[pos]=0;
    uint32_t current=fs->cwd;
    while(current) {
        uint32_t parent=disk_get(inode(fs,current)+8); unsigned char entries[DIR_COUNT*64];
        if(directory(fs,parent,entries)<0) return -1;
        const char *name=NULL;
        for(unsigned i=0;i<DIR_COUNT;++i) if(disk_get(entries+i*64)==current) { name=(char *)entries+i*64+4; break; }
        if(!name) { errno=EIO; return -1; }
        size_t len=strlen(name); if(pos<len+1) { errno=EOVERFLOW; return -1; }
        pos-=len; memcpy(path+pos,name,len); path[--pos]='/'; current=parent;
    }
    return emit(o,"%s\n",path+pos==path+sizeof path-1 ? "/" : path+pos);
}
static const char help[]=
    "Mon D.O.S. - commandes (chemins relatifs ou absolus)\n"
    "help | pwd | ls [chemin] | cd [chemin] | stat chemin | df\n"
    "mkdir chemin | touch chemin | cat chemin\n"
    "write chemin texte | append chemin texte | cp source destination\n"
    "mv source destination | rm fichier | rmdir repertoire | exit\n"
    "Guillemets simples/doubles ; \\n et \\t ; pas de commandes Linux externes.\n";
static int execute(Sgf *fs,int argc,char **v,Output *o,int *dirty) {
    if(!argc) return 0;
    const char *cmd=v[0]; uint32_t id=0;
    if(!strcmp(cmd,"help") && argc==1) return emit(o,"%s",help);
    if(!strcmp(cmd,"pwd") && argc==1) return print_path(fs,o);
    if(!strcmp(cmd,"cd") && argc<=2) {
        if(resolve(fs,argc==1 ? "/" : v[1],&id)<0) return -1;
        if(disk_get(inode(fs,id))!=KIND_DIR) { errno=ENOTDIR; return -1; }
        fs->cwd=id; return 0;
    }
    if(!strcmp(cmd,"df") && argc==1) {
        unsigned blocks=0,nodes=0;
        for(unsigned i=1;i<BLOCK_COUNT;++i) blocks+=(fs->image[MAP_B+i/8]>>(i%8))&1;
        for(unsigned i=0;i<INODE_COUNT;++i) nodes+=(fs->image[MAP_I+i/8]>>(i%8))&1;
        return emit(o,"Blocs utilises : %u/%u (512 octets)\nInodes utilises : %u/%u\n",
                    blocks,BLOCK_COUNT-1,nodes,INODE_COUNT);
    }
    if(!strcmp(cmd,"ls") && argc<=2) {
        if(resolve(fs,argc==1 ? "." : v[1],&id)<0) return -1;
        unsigned char entries[DIR_COUNT*64]; if(directory(fs,id,entries)<0) return -1;
        for(unsigned i=0;i<DIR_COUNT;++i) {
            uint32_t child=disk_get(entries+i*64); if(!child) continue;
            if(emit(o,"%s%s\n",entries+i*64+4,disk_get(inode(fs,child))==KIND_DIR ? "/" : "")<0) return -1;
        }
        return 0;
    }
    if(!strcmp(cmd,"stat") && argc==2) {
        if(resolve(fs,v[1],&id)<0) return -1;
        unsigned char *node=inode(fs,id);
        return emit(o,"inode=%u type=%s taille=%u octets parent=%u indirect=%s\n",id,
                    disk_get(node)==KIND_DIR ? "repertoire" : "fichier",disk_get(node+4),
                    disk_get(node+8),disk_get(node+44) ? "oui" : "non");
    }
    if((!strcmp(cmd,"mkdir") || !strcmp(cmd,"touch")) && argc==2) {
        *dirty=1;
        if(!strcmp(cmd,"touch") && resolve(fs,v[1],&id)==0) {
            if(disk_get(inode(fs,id))==KIND_DIR) { errno=EISDIR; return -1; } return 0;
        }
        return create_node(fs,v[1],!strcmp(cmd,"mkdir") ? KIND_DIR : KIND_FILE,&id);
    }
    if(!strcmp(cmd,"cat") && argc==2) {
        if(resolve(fs,v[1],&id)<0) return -1;
        if(disk_get(inode(fs,id))!=KIND_FILE) { errno=EISDIR; return -1; }
        size_t len=disk_get(inode(fs,id)+4);
        unsigned char data[FILE_MAX+1]; if(file_read(fs,id,data,FILE_MAX)<0) return -1;
        if(memchr(data,0,len)) { errno=EILSEQ; return -1; }
        data[len]=0; return emit(o,"%s",data);
    }
    if((!strcmp(cmd,"write") || !strcmp(cmd,"append")) && argc==3) {
        *dirty=1;
        if(resolve(fs,v[1],&id)<0) {
            if(errno!=ENOENT || create_node(fs,v[1],KIND_FILE,&id)<0) return -1;
        }
        if(disk_get(inode(fs,id))!=KIND_FILE) { errno=EISDIR; return -1; }
        unsigned char data[FILE_MAX]; size_t old=!strcmp(cmd,"append") ? disk_get(inode(fs,id)+4) : 0;
        size_t add=strlen(v[2]); if(old+add>FILE_MAX) { errno=EFBIG; return -1; }
        if(old && file_read(fs,id,data,sizeof data)<0) return -1;
        memcpy(data+old,v[2],add); return file_write(fs,id,data,old+add);
    }
    if((!strcmp(cmd,"rm") || !strcmp(cmd,"rmdir")) && argc==2) {
        *dirty=1; char name[60]; uint32_t parent;
        if(resolve(fs,v[1],&id)<0 || parent_name(fs,v[1],&parent,name)<0) return -1;
        uint32_t kind=disk_get(inode(fs,id));
        if(!strcmp(cmd,"rm") && kind==KIND_DIR) { errno=EISDIR; return -1; }
        if(!strcmp(cmd,"rmdir")) {
            unsigned char entries[DIR_COUNT*64]; if(directory(fs,id,entries)<0) return -1;
            for(unsigned i=0;i<DIR_COUNT;++i) if(disk_get(entries+i*64)) { errno=ENOTEMPTY; return -1; }
            uint32_t p=fs->cwd;
            for(;;) { if(p==id) { errno=EBUSY; return -1; } if(!p) break; p=disk_get(inode(fs,p)+8); }
        }
        if(dir_remove(fs,parent,name)<0) return -1;
        inode_free(fs,id); return 0;
    }
    if((!strcmp(cmd,"cp") || !strcmp(cmd,"mv")) && argc==3) {
        *dirty=1; char oldname[60],name[60]; uint32_t parent,oldparent,other;
        if(resolve(fs,v[1],&id)<0 || parent_name(fs,v[1],&oldparent,oldname)<0 ||
           parent_name(fs,v[2],&parent,name)<0) return -1;
        if(find(fs,parent,name,&other)==0) { errno=EEXIST; return -1; }
        if(errno!=ENOENT) return -1;
        if(!strcmp(cmd,"cp")) {
            if(disk_get(inode(fs,id))!=KIND_FILE) { errno=EISDIR; return -1; }
            unsigned char data[FILE_MAX]; size_t len=disk_get(inode(fs,id)+4);
            if(file_read(fs,id,data,sizeof data)<0 || create_node(fs,v[2],KIND_FILE,&other)<0) return -1;
            return file_write(fs,other,data,len);
        }
        uint32_t p=parent;
        for(;;) { if(p==id) { errno=EINVAL; return -1; } if(!p) break; p=disk_get(inode(fs,p)+8); }
        if(dir_remove(fs,oldparent,oldname)<0 || dir_add(fs,parent,name,id)<0) return -1;
        disk_put(inode(fs,id)+8,parent); return 0;
    }
    errno=EINVAL;
    return -1;
}
int sgf_execute(Sgf *fs,const char *command,char *output,size_t capacity) {
    if(!capacity) { errno=EINVAL; return -1; } output[0]=0;
    if(strlen(command)>=COMMAND_MAX) { snprintf(output,capacity,"Erreur : commande trop longue\n"); return -1; }
    char line[COMMAND_MAX],*args[8]; strcpy(line,command);
    int argc=parse(line,args,8);
    if(argc<0) { snprintf(output,capacity,"Erreur : syntaxe invalide (guillemets ou arguments)\n"); return -1; }
    unsigned char *backup=malloc(IMAGE_SIZE);
    if(!backup) { snprintf(output,capacity,"Erreur : memoire insuffisante\n"); return -1; }
    memcpy(backup,fs->image,IMAGE_SIZE); uint32_t cwd=fs->cwd;
    Output o={output,capacity,0}; int dirty=0;
    int result=execute(fs,argc,args,&o,&dirty);
    if(result==0 && dirty) result=disk_save(fs);
    if(result<0) {
        int saved=errno; memcpy(fs->image,backup,IMAGE_SIZE); fs->cwd=cwd;
        snprintf(output,capacity,"Erreur : %s (help pour la syntaxe)\n",strerror(saved));
        errno=saved;
    }
    free(backup); return result;
}
