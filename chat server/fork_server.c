#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "deamon.h"

#define TCP_PORT   5100
#define MAX_CLIENT 32;

// 각 자식 프로세스(클라이언트 핸들러)의 정보를 담는 구조체
typedef struct {
    // 자식 프로세스의 PID
    pid_t pid;             
    // 부모가 이 자식에게 메시지를 보낼 때 사용하는 파이프의 '쓰기' 끝 FD (부모 관점)
    int child_write_fd;
    // 이 자식이 부모에게 메시지를 보낼 때, 부모가 '읽을' 파이프의 FD (부모 관점)
    int parent_read_fd;
} pipeInfo;

int main(int argc, char **argv)
{
    int ssock;  //서버소켓 (클라이언트 소켓이 연결되었을때만 사용)
    int csock; //클라이언트 소켓 
    socklen_t clen; //주소 구조체  길이를 저장할 변수 
    struct sockaddr_in servaddr, cliaddr;//클라이언트의 주소정보를 담을 빈그릇
    char mesg[BUFSIZ]; //메시지 읽는거
    struct sigaction sa; //SIGIO를 쓰기 위한 안정된 비동기 핸들러
    
    daemonize(argv); //데몬화
    sa.sa_handler = SIGIO; //SIGIO 핸들러에 등록
    //서버소켓 생성
    if((ssock = socket(AF_INET,SOCK_STREAM, 0))<0){
        syslog(LOG_ERR,"socket not create");
        exit(1);
    }
    //서버소켓 설정
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);
    //서버소켓 연결
    if(bind(ssock,(struct sockaddr *)&servaddr,sizeof(servaddr))<0){
        syslog(LOG_ERR,"No Bind");
        exit(1);
    }
    //서버소켓 가동 
    if(listen(ssock,8)<0){
        syslog(LOG_ERR,"Cannot listen");
        exit(1);
    }
    //clen이 클라이언트 주소 정보를 받을 cliaddr라는 
    //"버퍼"의 크기를 accept() 함수에 알려주는 역할
    clen = sizeof(cliaddr); 
    //클라이언트 여러개를 받기 위한 선언 
    struct pipeInfo pipe_info[MAX_CLIENT]; //클라이언트 정보 참조
    int client_num = 0; //활성화된 클라이언트
    do{
        //클라이언트 연결 감지중 
        int n, csock = accept(ssock,(struct sockaddr *)&cliaddr,&clen);
        //파이프 관련 초기화 
        pid_t new_pid; //부모 자식 구분자
        int parent_pfd[2]; //부모 fd
        int child_pfd[2]; //자식 fd 
        
        pipe_info[client_num].pid = new_pid;
        pipe_info[client_num].child_write_fd  = parent_pfd[2];
        pipe_info[client_num].parent_read_fd  = child_pfd[2];
        
        //디버깅용 코드
        //클라이언트의 IP 주소를 사람이 읽을 수 있는 문자열 형태로 변환하는 역할
        inet_ntop(AF_INET, &cliaddr.sin_addr,mesg,BUFSIZ);
        syslog(LOG_INFO,"Client is connected : %s",mesg);

        //자식 --> 부모 파이프 생성
        if(pipe(pipe_info[client_num].child_write_fd)<0){ 
            syslog(LOG_ERR,"No CHILD PIPE!");
            exit(1);
        }
        //부모 --> 자식 파이프 생성
        if(pipe(pipe_info[client_num].parent_read_fd)<0){ 
            syslog(LOG_ERR,"No PARENT PIPE!");
            exit(1);
        }

        if((pipe_info[client_num].pid = fork())<0){
           syslog(LOG_ERR,"NO FORK!!");
            exit(1);
        }
        else if(pipe_info[client_num].pid == 0){ //자식
            close(pipe_info[client_num].child_write_fd[0]);
            
            close(pipe_info[client_num].child_write_fd[1]);
        }
        else{ //부모
            close(pipe_info[client_num].parent_read_fd[1]);
            
            close(pipe_info[client_num].parent_read_fd[0]);
            waitpid(pid,&status,0);
        }

        if((n=read(csock,mesg,BUFSIZ)) <= 0){
            syslog(LOG_ERR,"cannot read client message");
        }

        syslog(LOG_INFO,"Received data : %s",mesg);

        if(write(csock,mesg,n) <= 0)
            syslog(LOG_ERR,"cannot Write");
        close(csock);
    }while(strncmp(mesg,"q",1));

    close(ssock);

    return 0;
}