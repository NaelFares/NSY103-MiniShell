#define _POSIX_C_SOURCE 200809L
#include "display.h"
#include "protocol.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <time.h>
#include <unistd.h>
typedef struct {
    long type;
    uint32_t magic,kind,length;
    char data[DISPLAY_CHUNK];
} Message;
#define HEADER_BYTES (3u*sizeof(uint32_t))
key_t display_default_key(void) { return (key_t)(0x4d000000u|((unsigned)getuid()&0xffffffu)); }
int display_parse_key(const char *text,key_t *key) {
    char *end; errno=0; long value=strtol(text,&end,0);
    if(errno || !*text || *end || value<=0 || value>INT_MAX) { errno=EINVAL; return -1; }
    *key=(key_t)value; return 0;
}
int display_create(key_t key) { return msgget(key,IPC_CREAT|IPC_EXCL|0600); }
int display_connect(key_t key) {
    int q=msgget(key,0600); if(q<0) return -1;
    struct msqid_ds info; if(msgctl(q,IPC_STAT,&info)<0) return -1;
    if(info.msg_perm.uid!=getuid() || (info.msg_perm.mode&0777)!=0600) { errno=EACCES; return -1; }
    return q;
}
static int send_message(int queue,Message *msg) {
    struct timespec start,now,pause={0,20000000};
    if(clock_gettime(CLOCK_MONOTONIC,&start)<0) return -1;
    for(;;) {
        if(msgsnd(queue,msg,HEADER_BYTES+msg->length,IPC_NOWAIT)==0) return 0;
        if(errno!=EAGAIN) return -1;
        if(clock_gettime(CLOCK_MONOTONIC,&now)<0) return -1;
        if(now.tv_sec-start.tv_sec>=5) { errno=ETIMEDOUT; return -1; }
        if(nanosleep(&pause,NULL)<0) return -1;
    }
}
int display_send(int queue,const char *text,int end) {
    size_t length=strlen(text),pos=0;
    while(pos<length) {
        Message msg={.type=1,.magic=FRAME_MAGIC,.kind=DISPLAY_TEXT};
        size_t n=length-pos; if(n>DISPLAY_CHUNK) n=DISPLAY_CHUNK;
        msg.length=(uint32_t)n; memcpy(msg.data,text+pos,n);
        if(send_message(queue,&msg)<0) return -1;
        pos+=n;
    }
    if(end) {
        Message msg={.type=1,.magic=FRAME_MAGIC,.kind=DISPLAY_END,.length=0};
        return send_message(queue,&msg);
    }
    return 0;
}
int display_receive(int queue,char *text,size_t capacity) {
    Message msg;
    ssize_t n=msgrcv(queue,&msg,HEADER_BYTES+DISPLAY_CHUNK,1,IPC_NOWAIT);
    if(n<0) return -1;
    if((size_t)n<HEADER_BYTES || msg.magic!=FRAME_MAGIC || msg.length>DISPLAY_CHUNK ||
       (size_t)n!=HEADER_BYTES+msg.length || msg.length>=capacity ||
       (msg.kind!=DISPLAY_TEXT && msg.kind!=DISPLAY_END) ||
       (msg.kind==DISPLAY_END && msg.length) || memchr(msg.data,0,msg.length)) { errno=EPROTO; return -1; }
    memcpy(text,msg.data,msg.length); text[msg.length]=0;
    return msg.kind==DISPLAY_END ? 0 : 1;
}
int display_remove(int queue) { return msgctl(queue,IPC_RMID,NULL); }
