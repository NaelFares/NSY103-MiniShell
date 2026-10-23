#define _POSIX_C_SOURCE 200809L
#include "kernel.h"
#include "protocol.h"
#include "sgf.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
int command_is_exit(const char *command) {
    while(isspace((unsigned char)*command)) ++command;
    if(strncmp(command,"exit",4)) return 0;
    command+=4;
    while(isspace((unsigned char)*command)) ++command;
    return !*command;
}
int kernel_run(int requests,int responses,const char *disk,int create) {
    char error[512]; Sgf *fs=sgf_open(disk,create,error,sizeof error);
    if(!fs) { frame_send(responses,error,1); return 1; }
    if(frame_send(responses,"Disque pret.\n",0)<0) { sgf_close(fs); return 1; }
    char command[COMMAND_MAX]; char *output=malloc(OUTPUT_MAX);
    if(!output) { sgf_close(fs); return 1; }
    int result=0;
    for(;;) {
        int status,r=frame_recv(requests,command,sizeof command,&status);
        if(r<=0) { if(r<0) result=1; break; }
        if(status) { result=1; break; }
        if(command_is_exit(command)) {
            if(frame_send(responses,"Fin de session.\n",0)<0) result=1;
            break;
        }
        r=sgf_execute(fs,command,output,OUTPUT_MAX);
        if(frame_send(responses,*output ? output : "OK\n",r)<0) { result=1; break; }
    }
    free(output); sgf_close(fs); return result;
}
