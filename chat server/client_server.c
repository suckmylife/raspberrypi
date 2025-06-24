#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h> 
#include <netinet/in.h>

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

typedef struct {
	int type;
	char msg[BUFSIZ-4];
} data_t;

static int g_pfd[2], g_sockfd, g_cont = 1;

// 위의 선언없이 extern inline void clrscr(void)로 선언
inline void clrscr(void);		// C99, C11에 대응하기 위해서 사용
void clrscr(void)				
{
    write(1, "\033[1;1H\033[2J", 10);		// ANSI escape 코드로 화면 지우기
}

void sigHandler(int signo)
{
	if(signo == SIGUSR1) { 
		char buf[BUFSIZ];
		int n = read(g_pfd[0], buf, BUFSIZ);
		write(g_sockfd, buf, n);
	} else if(signo == SIGCHLD) {
		g_cont = 0;
		printf("Connection is lost\n");
	}
}

int main(int argc, char** argv)
{ 
	struct sockaddr_in servaddr;
	int pid;
	char buf[BUFSIZ]; 

	clrscr();

	if(argc < 3) {
		fprintf(stderr, "usage : %s IP_ADDR PORT_NO\n", argv[0]);
		return -1;
	}

	g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(g_sockfd < 0) {
		perror("socket");
		return -1;
	}

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
	servaddr.sin_port = htons(atoi(argv[2]));
	connect(g_sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

	pipe(g_pfd);
	if((pid = fork()) < 0) {
		perror("fork( )");
	} else if (pid == 0) {
		signal(SIGCHLD, sigHandler);
		close(g_pfd[0]);
		do { 
			memset(buf, 0, BUFSIZ); 
			printf(COLOR_BLUE "\r> " COLOR_RESET);
			fflush(NULL);
			fgets(buf, BUFSIZ, stdin);
			write(g_pfd[1], buf, strlen(buf)+1);
			kill(getppid( ), SIGUSR1);
		} while (strcmp(buf, "quit") && g_cont);
		close(g_pfd[1]);
	} else { 			// pid > 0
		signal(SIGUSR1, sigHandler);
		signal(SIGCHLD, sigHandler);
		close(g_pfd[1]);
		while(g_cont) { 
			memset(buf, 0, BUFSIZ);
			int n = read(g_sockfd, buf, BUFSIZ);
			if(n <= 0) break;
			printf(COLOR_GREEN "\r%s" COLOR_RESET, buf);
			printf(COLOR_BLUE "\r> " COLOR_RESET);
			fflush(NULL);
		}
		close(g_pfd[0]);
		kill(pid, SIGCHLD);
		wait(NULL);
	}

	close(g_sockfd);

	return 0;
}
