#define _POSIX_C_SOURCE 200809L
#include "display.h"
#include "kernel.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
static volatile sig_atomic_t stopped=0;
static void stop(int sig) { (void)sig; stopped=1; }
static int session_lock(key_t key) {
    char path[128]; snprintf(path,sizeof path,"/tmp/mdos-%lu-%ld.shell.lock",(unsigned long)getuid(),(long)key);
    int fd=open(path,O_CREAT|O_RDWR|O_NOFOLLOW,0600);
    if(fd<0) return -1;
    if(flock(fd,LOCK_EX|LOCK_NB)<0) { close(fd); return -1; }
    return fd;
}
int main(int argc,char **argv) {
    const char *disk="disque.dat"; int create=0; key_t key=display_default_key();
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--format")) create=1;
        else if(!strcmp(argv[i],"--disk") && i+1<argc) disk=argv[++i];
        else if(!strcmp(argv[i],"--key") && i+1<argc) {
            if(display_parse_key(argv[++i],&key)<0) { fprintf(stderr,"Cle invalide\n"); return 2; }
        } else { fprintf(stderr,"Usage : %s [--disk fichier.dat] [--format] [--key entier]\n",argv[0]); return 2; }
    }
    signal(SIGPIPE,SIG_IGN);
    struct sigaction action={0}; action.sa_handler=stop; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL);
    int queue=display_connect(key);
    if(queue<0) { fprintf(stderr,"Lancez Ecran avant le Shell (cle %ld) : %s\n",(long)key,strerror(errno)); return 1; }
    int lock=session_lock(key);
    if(lock<0) { fprintf(stderr,"Une session utilise deja cet Ecran : %s\n",strerror(errno)); return 1; }
    int request[2],response[2];
    if(pipe(request)<0) { perror("pipe commandes"); close(lock); return 1; }
    if(pipe(response)<0) { perror("pipe reponses"); close(request[0]); close(request[1]); close(lock); return 1; }
    pid_t child=fork();
    if(child==0) {
        signal(SIGINT,SIG_IGN); signal(SIGTERM,SIG_DFL);
        close(request[1]); close(response[0]); close(lock);
        int r=kernel_run(request[0],response[1],disk,create);
        close(request[0]); close(response[1]); _exit(r);
    }
    close(request[0]); close(response[1]);
    if(child<0) { perror("fork"); close(request[1]); close(response[0]); close(lock); return 1; }
    int result=0,status=0; char *output=malloc(OUTPUT_MAX),*line=NULL; size_t capacity=0;
    if(!output) { result=1; goto cleanup; }
    if(frame_recv(response[0],output,OUTPUT_MAX,&status)!=1) { perror("Initialisation Noyau"); result=1; goto cleanup; }
    if(display_send(queue,output,0)<0) { perror("Affichage"); result=1; goto cleanup; }
    if(status) { fprintf(stderr,"%s\n",output); result=1; goto cleanup; }
    fprintf(stderr,"Mon D.O.S. : help pour les commandes, exit pour terminer.\n");
    while(!stopped) {
        if(isatty(STDIN_FILENO)) { fputs("MonDOS> ",stderr); fflush(stderr); }
        ssize_t length=getline(&line,&capacity,stdin);
        const char *command=line;
        if(length<0) {
            if(ferror(stdin) && !stopped) { perror("Lecture commande"); result=1; }
            command="exit";
        } else {
            if((size_t)length>=COMMAND_MAX || memchr(line,0,(size_t)length)) {
                if(display_send(queue,"Erreur : commande trop longue ou octet nul\n",0)<0) { result=1; break; }
                result=1; continue;
            }
            size_t i=0; while(line[i]==' ' || line[i]=='\t' || line[i]=='\r' || line[i]=='\n') ++i;
            if(!line[i]) continue;
        }
        if(frame_send(request[1],command,0)<0 || frame_recv(response[0],output,OUTPUT_MAX,&status)!=1) {
            fprintf(stderr,"Communication avec le Noyau interrompue\n"); result=1; break;
        }
        if(display_send(queue,output,0)<0) { perror("Affichage"); result=1; break; }
        if(status) result=1;
        if(command_is_exit(command)) break;
    }
cleanup:
    /* Closing the request pipe also wakes the Noyau when interrupted. */
    close(request[1]); close(response[0]); free(output); free(line);
    int child_status=0; pid_t waited;
    do { waited=waitpid(child,&child_status,0); } while(waited<0 && errno==EINTR);
    if(waited<0 || !WIFEXITED(child_status) || WEXITSTATUS(child_status)) result=1;
    if(display_send(queue,"",1)<0) { perror("Fin affichage"); result=1; }
    close(lock); return stopped ? 128+SIGINT : result;
}
