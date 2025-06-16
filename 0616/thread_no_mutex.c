#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>

int g_var = 1;
void *inc_function(void *);
void *dec_function(void *);

int main(int argc, char **argv)
{
    pthread_t ptInc, ptDec;
    pthread_create(&ptInc, NULL, inc_function,NULL);
    pthread_create(&ptDec, NULL, dec_function,NULL);
}

void *inc_function(void *arg)
{
    printf("INC : %d < before\n",g_var);
    fflush(stdout);
    g_var++;
    printf("INC : %d > After \n",g_var);
    fflush(stdout);
    return NULL;
}

void *dec_function(void *arg)
{
    printf("DEC : %d < before\n",g_var);
    fflush(stdout);
    g_var--;
    printf("DEC : %d > After \n",g_var);
    fflush(stdout);
    return NULL;
}