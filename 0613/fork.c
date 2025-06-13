#include <stdio.h>
#include <unistd.h>

static int g_var = 1;
char str[] = "PID";

int main(int arg, char **argv)
{
    int var;
    pid_t pid;
    var = 92;

    if((pid = fork())<0){
        perror("[ERROR] : fork()");
    }
    else if(pid == 0){
        g_var++;
        var++;
        printf("Child %s from Parent Process(%d) : %d \n",str, getpid(),getppid());
    }
    else{
         printf("Parent %s from Child Process(%d) : %d \n",str, getpid(),pid);
         sleep(1);
    }
    printf("pid = %d, Global var = %d, var = %d \n",getpid(),g_var,var);
    return 0;
}