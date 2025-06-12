#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void printSigset(sigset_t *set);
static void sigHandler(int);

int main(int argc, char **argv)
{
    sigset_t pset;
    sigemptyset(&pset);
    sigaddset(&pset,SIGQUIT);
    sigaddset(&pset, SIGTMIN);
    sigprocmask(SIG_BLOCK, &pset, NULL);

    printSigset(&pset);

    if(signal(SIGINT, sigHandler) == SIG_ERR){
        perror("signal() : SIGINT");
        return -1;
    }
    if(signal(SIGUSR1, sigHandler) == SIG_ERR){
        perror("signal() : SIGUSR1");
        return -1;
    }
    if(signal(SIGUSR2, sigHandler) == SIG_ERR){
        perror("signal() : SIGUSR2");
        return -1;
    }
    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR){
        perror("signal() : SIGPIPE");
        return -1;
    }
    while(1) pause();
    return 0;
    
}

static void sigHandler(int signo)
{
    if(signo == SIGINT){
        printf("SIGINT is catched : %d\n",signo);
        exit(0);
    }
    else if(signo == SIGUSR1){
        printf("SIGUSR1 is catched\n");
    }
    else if(signo == SIGUSR2){
        printf("SIGUSR2 is catched\n");
    }
    else if(signo == SIGQUIT){
        printf("SIGUSR2 is catched\n");
        sigset_t uset;
        sigemptyset(&uset);
        sigaddset(&uset,SIGINT);
        sigprocmask(SIG_UNBLOCK,&uset,NULL);
    }
    else{
        fprintf(stderr,"Catched signal : %s\n", strsignal(signo));
    }
}

static void printSigset(sigset_t *set)
{
    int i;
    for(i = 1; i<NSIG; ++i){
        printf((sigismember(set,i))?"1":"0");
    }
    putchar('\n');
}