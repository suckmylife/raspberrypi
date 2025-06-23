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
#include "common.h"
#include "signals.h"

int main(int argc, char **argv)
{
    int ssock;  //서버소켓 (클라이언트 소켓이 연결되었을때만 사용)
    int csock; //클라이언트 소켓 
    socklen_t cli_len; //주소 구조체  길이를 저장할 변수 
    struct sockaddr_in servaddr, cliaddr;//클라이언트의 주소정보를 담을 빈그릇
    
    daemonize(argv); //데몬화
    
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
    cli_len = sizeof(cliaddr); 
    //클라이언트 여러개를 받기 위한 선언 
    
    client_num = 0; //활성화된 클라이언트
    do{
        //자식이 죽었음을 알리는 플래그
        if(child_exited_flag){
            clean_active_process();
            child_exited_flag = 0;
        }
        //클라이언트 연결 감지중 
        int n, csock = accept(ssock,(struct sockaddr *)&cliaddr,&cli_len);
        //파이프 관련 초기화 
        pid_t new_pid; //부모 자식 구분자
        int parent_pfd[2]; //부모->자식 fd
        int child_pfd[2];  //자식->부모 fd 
        
        //디버깅용 코드
        //클라이언트의 IP 주소를 사람이 읽을 수 있는 문자열 형태로 변환하는 역할
        inet_ntop(AF_INET, &cliaddr.sin_addr,mesg,BUFSIZ);
        syslog(LOG_INFO,"Client is connected : %s",mesg);

        //자식 --> 부모 파이프 생성
        if(pipe(child_pfd)<0){ 
            syslog(LOG_ERR,"No CHILD PIPE!");
            exit(1);
        }
        //부모 --> 자식 파이프 생성
        if(pipe(parent_pfd)<0){ 
            syslog(LOG_ERR,"No PARENT PIPE!");
            exit(1);
        }
        //클라이언트 서버 프로세스 생성 
        if((new_pid = fork())<0){
            syslog(LOG_ERR,"NO FORK!!");
            exit(1);
        }
        else if(new_pid == 0){ //자식 : 클라이언트에서 쓴걸 읽고 부모에게 보낸다
            pid
            char mesg[BUFSIZ]; //메시지 읽는거
            close(ssock); //자식은 클라이언트를 감지 하지 않아도 되니까 닫음

            //부모는 parent_pfd[1]에 쓰고, 자식은 parent_pfd[0]에서 읽음
            //자식은 읽어야 하니까 반대를 닫는다
            close(parent_pfd[1]); 
            //자식은 child_pfd[1]에 쓰고, 부모는 child_pfd[0]에서 읽음
            //자식이 써야 하니까 반대를 닫는다
            close(child_pfd[0]);
            while(1){
                //클라이언트에서 메시지를 받는다
                if((n=read(csock,mesg,BUFSIZ)) <= 0){
                    syslog(LOG_ERR,"cannot read client message");
                    break;
                }
                else{
                    mesg[n] = '\0';
                    syslog(LOG_INFO,"Received data : %s",mesg);
                    if(write(child_pfd[1],mesg,n) <= 0) //클라이언트에게 받은걸 부모에게 쓴다.
                        syslog(LOG_ERR,"cannot Write to parent");
                    kill();
                }
                //부모의 메시지를 읽는다
                if((n=read(parent_pfd[0],mesg,BUFSIZ)) <= 0){
                    syslog(LOG_ERR,"cannot read SERVER message");
                    break;
                }
                else{
                    mesg[n] = '\0';
                    syslog(LOG_INFO,"Received data : %s",mesg);
                    if(write(csock,mesg,n) <= 0) //클라이언트에게 받은걸 부모에게 쓴다.
                        syslog(LOG_ERR,"cannot Write to client");
                }
            }
            close(csock); //할일 다 했으니까 종료
            //열어놓은 파이프 닫기
            close(parent_pfd[0]);
            close(child_pfd[1]); 
        }
        else{ //부모 : 자식이 보낸걸 읽고 
            char mesg[BUFSIZ]; //메시지 읽는거
            close(csock); // 부모는 클라이언트 소켓을 쓰지 않으니까
            //부모는 parent_pfd[1]에 쓰고, 자식은 parent_pfd[0]에서 읽음
            //부모는 써야 하니까 반대를 닫는다
            close(parent_pfd[0]); 
            //자식은 child_pfd[1]에 쓰고, 부모는 child_pfd[0]에서 읽음
            //부모는 읽어야 하니까 반대를 닫는다
            close(child_pfd[1]);
            pipe_info[client_num].pid = new_pid;
            //부모가 자식에게 쓰고 있는 파이프의 단 할당
            pipe_info[client_num].parent_to_child_write_fd  = parent_pfd[1];
            //자식이 부모에게 온걸 읽을 수 있는 파이프의 단 할당
            pipe_info[client_num].child_to_parent_read_fd  = child_pfd[0];
            client_num++;
            if(client_num >= MAX_CLIENT){
                syslog(LOG_ERR,"Index out of client number");
                exit(1);
            }

            for(int i = 0; i<client_num; i++)
            {
                //자식의 메시지를 읽는다
                if((n=read(pipe_info[i].child_to_parent_read_fd,mesg,BUFSIZ)) <= 0){
                    syslog(LOG_ERR,"cannot read child message");
                    break;
                }
                else{
                    //다른 자식프로세스에 메시지를 보낸다.
                    mesg[n] = '\0';
                    for(int j = 0; j < client_num; j++){
                        syslog(LOG_INFO,"Received data : %s",mesg);
                        if(write(pipe_info[j].parent_to_child_write_fd,mesg,n) <= 0) 
                            syslog(LOG_ERR,"cannot Write to child");
                    }
                    
                }
            }
            close(ssock);
            //열어놓은 파이프 닫기
            close(parent_pfd[1]);
            close(child_pfd[0]); 
        }
        
    }while(!is_shutdown);

    close(ssock);

    return 0;
}
