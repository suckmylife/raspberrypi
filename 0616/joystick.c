#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/input.h>

#define JOY_DEV "/dev/input/event1"

int main()
{
    int fd;
    struct input_event ie;

    if((fd = open(JOY_DEV,O_RDONLY))==-1){
        perror("opening device");
        exit(EXIT_FAILURE);
    }
    printf("here?\n");
    fflush(stdout);
    while(read(fd,&ie,sizeof(struct input_event))){
        printf("while here?\n");
        fflush(stdout);
        printf("time %ld.%06ld\ttype %d\tcode %-3d\tvalue %d\n",
                ie.time.tv_sec,ie.time.tv_usec, ie.type, ie.code,ie.value);
        if(ie.type){
            switch (ie.code)
            {
            case KEY_UP: printf("Up\n"); break;
            case KEY_DOWN: printf("Down\n"); break;
            case KEY_LEFT: printf("Left\n"); break;
            case KEY_RIGHT: printf("Right\n"); break;
            case KEY_ENTER: printf("Enter\n"); break;
            default:printf("Default\n"); break;
            }
        }
    }
    return 0;
}