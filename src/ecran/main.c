#define _POSIX_C_SOURCE 200809L
#include "display.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stopped=0;
static void stop(int sig) { (void)sig; stopped=1; }
int main(int argc,char **argv) {
    key_t key=display_default_key();
    if(argc!=1 && (argc!=3 || strcmp(argv[1],"--key") || display_parse_key(argv[2],&key)<0)) {
        fprintf(stderr,"Usage : %s [--key entier]\n",argv[0]); return 2;
    }
    struct sigaction action={0}; action.sa_handler=stop; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL);
    int queue=display_create(key);
    if(queue<0) { fprintf(stderr,"Ecran : %s (cle %ld ; verifier ipcs -q)\n",strerror(errno),(long)key); return 1; }
    fprintf(stderr,"Ecran pret, cle %ld. Lancez le Shell dans le terminal 1.\n",(long)key);
    char text[DISPLAY_CHUNK+1]; int result=0;
    while(!stopped) {
        int r=display_receive(queue,text,sizeof text);
        if(r==0) break;
        if(r<0) {
            if(errno==ENOMSG || errno==EINTR) { struct timespec delay={0,20000000}; nanosleep(&delay,NULL); continue; }
            fprintf(stderr,"Ecran : %s\n",strerror(errno)); result=1; break;
        }
        if(fputs(text,stdout)==EOF || fflush(stdout)==EOF) { result=1; break; }
    }
    if(display_remove(queue)<0 && errno!=EINVAL && errno!=EIDRM) result=1;
    return result;
}
