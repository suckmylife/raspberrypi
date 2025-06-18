#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define SERVER_PORT 5100 			/* 서버의 포트 번호 */
#define MAX_EVENT 	2

/* 파일 디스크립터를 넌블로킹 모드로 설정 */
void setnonblocking(int fd)
{
    int opts = fcntl(fd, F_GETFL);
    opts |= O_NONBLOCK;
    fcntl(fd, F_SETFL, opts);
}

int main(int argc, char **argv)
{
    int ssock, n;
    struct sockaddr_in servaddr;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENT];
    char mesg[BUFSIZ];

	if(argc < 2) {
		printf("usage : %s IP_ADDR\n", argv[0]);
		return -1;
	}

    /* 서버 소켓 디스크립터를 연다. */
    if((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr)); 	/* 운영체제에 서비스 등록 */
    servaddr.sin_family = AF_INET;
	inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(SERVER_PORT);
	if(connect(ssock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }

    setnonblocking(ssock); 			/* 서버를 넌블로킹 모드로 설정 */

    /* epoll_create() 함수를 이용해서 커널에 등록 */
    int epfd = epoll_create(MAX_EVENT);
    if(epfd == -1) {
        perror("epoll_create()");
        return 1;
    }

    /* epoll_ctl() 함수를 통해 감시하고 싶은 서버 소켓을 등록 */
    ev.events = EPOLLIN;
    ev.data.fd = 0;
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &ev) == -1) {
        perror("epoll_ctl()");
        return 1;
    }

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = ssock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ssock, &ev);

    do {
        epoll_wait(epfd, events, MAX_EVENT, 500);
        for(int i = 0; i < MAX_EVENT; i++) {
            memset(mesg, 0, sizeof(mesg));
            if(events[i].data.fd == 0) { 
                n = read(0, mesg, sizeof(mesg)); 
             	write(ssock, mesg, n);
            } else if(events[i].data.fd == ssock) { 
                if(events[i].data.fd < 0) continue; 	/* 소켓이 아닌 경우의 처리 */
                n = read(events[i].data.fd, mesg, sizeof(mesg)); 
				if(n <= 0) break;
             	write(1, mesg, n);
            }
        }
    } while(strncmp(mesg, "q", 1));

    close(ssock); 				/* 서버 소켓을 닫는다. */

    return 0;
}