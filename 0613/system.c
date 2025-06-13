#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

int system(const char *cmd)
{
    pid_t pid;
    int status;

    if((pid = fork())<0){
        status = -1;
    }
    else if(pid == 0){
        execl("/bin/sh","sh","-c",cmd,(char *)0);
        _exit(127);
    }
    else{
        while(waitpid(pid,&status,0)<0)
        if(errno != EINTR){
            status = -1;
            break;
        }
    }
    return  status;
}

int main(int argc, char **argv, char **envp)
{
    while(*envp)
        printf("%s\n",*envp++);
    system("who");
    system("nocommand");
    system("cal");
    return 0;
}