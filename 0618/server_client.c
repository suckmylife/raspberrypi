#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define SERVER_PORT 5100

int main(int argc, char **argv)
{
    int ssock;
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];
    fd_set readfd;
    int maxfd;

    if(argc < 2) {
        printf("usage : %s IP_ADDR\n", argv[0]);
        return 1;
    }

    if((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return 1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(SERVER_PORT);

    if(connect(ssock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return 1;
    }

    FD_ZERO(&readfd);
    do {
        memset(mesg, 0, BUFSIZ);
        FD_SET(0, &readfd);
        FD_SET(ssock, &readfd);
        maxfd = ssock + 1;
        select(maxfd, &readfd, NULL, NULL, NULL);
        if(FD_ISSET(0, &readfd)) { 
            fgets(mesg, BUFSIZ, stdin);
            write(ssock, mesg, BUFSIZ);
        }

        if(FD_ISSET(ssock, &readfd)) { 
            read(ssock, mesg, BUFSIZ);
            printf("%s", mesg);
        }
    } while (strcmp("msg", "quit"));


    close(ssock);
    
    return 0;
}