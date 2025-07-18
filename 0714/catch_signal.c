#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

void signal_handler(int signum)
{
    printf("Signal is Catched!!\n");
    if(signum == SIGIO){
        printf("SIGIO\r\n");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    char buf[BUFSIZ];
    char i = 0;
    int fd = -1;
    memset(buf,0,BUFSIZ);

    signal(SIGIO, signal_handler);
    printf("GPIO set : %s\n",argv[1]);
    fd = open("/dev/gpioled",O_RDWR);
    sprintf(buf,"%s:%d\n",argv[1],getpid());

    write(fd,buf,strlen(buf));

    if(read(fd, buf, strlen(buf))!= 0){
        printf("Success : read()\n");
    }
    printf("Read Data : %s \n",buf);
    printf("My PID is %d\n",getpid());
    while(1);
    close(fd);
    return 0;
}