#include <stdio.h>
#include <unistd.h>

static int g_var = 1;
char str[] = "PID";

int main(int arg, char **argv)
{
    int var;
    pid_t pid;
    var = 88;

    if((pid = vfork())<0){
        perror("[ERROR] : vfork()");
    }
    else if(pid == 0){
        g_var++;
        var++;
        printf("Parent %s from Child Process(%d) : %d \n",str, getpid(),getppid());
        printf("pid = %d, Global var = %d, var = %d \n",getpid(),g_var,var);
        _exit(0);
    }
    else{
         printf("Child %s from Parent Process(%d) : %d \n",str, getpid(),pid);
    }
    printf("pid = %d, Global var = %d, var = %d \n",getpid(),g_var,var);
    return 0;
}