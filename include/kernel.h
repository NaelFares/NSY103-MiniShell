#ifndef KERNEL_H
#define KERNEL_H
int kernel_run(int requests,int responses,const char *disk,int create);
int command_is_exit(const char *command);
#endif
