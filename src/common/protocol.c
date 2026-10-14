#include "protocol.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

static void put32(unsigned char *p, uint32_t v) {
    for (unsigned i=0; i<4; ++i) p[i]=(unsigned char)(v>>(8*i));
}
static uint32_t get32(const unsigned char *p) {
    uint32_t v=0; for (unsigned i=0; i<4; ++i) v|=(uint32_t)p[i]<<(8*i); return v;
}
int write_full(int fd, const void *data, size_t size) {
    const unsigned char *p=data;
    while(size) {
        ssize_t n=write(fd,p,size);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return -1;
        p+=n; size-=(size_t)n;
    }
    return 0;
}
static int read_full(int fd, void *data, size_t size, int header) {
    unsigned char *p=data; size_t done=0;
    while(done<size) {
        ssize_t n=read(fd,p+done,size-done);
        if(n<0 && errno==EINTR) continue;
        if(n<0) return -1;
        if(n==0) {
            if(header && done==0) return 0;
            errno=EPROTO; return -1;
        }
        done+=(size_t)n;
    }
    return 1;
}
int frame_send(int fd, const char *text, int status) {
    size_t len=strlen(text); unsigned char h[16];
    if(len>=OUTPUT_MAX) { errno=EMSGSIZE; return -1; }
    put32(h,FRAME_MAGIC); put32(h+4,1); put32(h+8,(uint32_t)len);
    put32(h+12,status ? 1u : 0u);
    if(write_full(fd,h,sizeof h)<0) return -1;
    return write_full(fd,text,len);
}
int frame_recv(int fd, char *text, size_t capacity, int *status) {
    unsigned char h[16]; int r=read_full(fd,h,sizeof h,1);
    if(r<=0) return r;
    uint32_t len=get32(h+8);
    if(get32(h)!=FRAME_MAGIC || get32(h+4)!=1 || len>=capacity ||
       get32(h+12)>1) { errno=EPROTO; return -1; }
    if(read_full(fd,text,len,0)<0) return -1;
    if(memchr(text,0,len)) { errno=EPROTO; return -1; }
    text[len]=0; *status=(int)get32(h+12); return 1;
}
