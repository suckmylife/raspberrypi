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

#include "deamon.h"
#include "common.h"
#include "signals.h"
#include "clientmanager.h"

int check_command(const char* mesg, const char* command);

int main(int argc, char **argv)
{
    int ssock;                                      //서버소켓 (클라이언트 소켓이 연결되었을때만 사용)
    int csock;                                      //클라이언트 소켓 
    socklen_t cli_len;                              //주소 구조체  길이를 저장할 변수 
    struct sockaddr_in servaddr, cliaddr;           //클라이언트의 주소정보를 담을 빈그릇
    roomInfo room_info[CHAT_ROOM] = {0};            // 메모리 할당 (초기화 포함)
    pipeInfo client_pipe_info[CHAT_ROOM] = {0};     // 메모리 할당 (초기화 포함)

    //시그널 등록
    setup_client_handler();
    setup_chatroom_handler();
    child_close_handler();

    daemonize(argc, argv);                          //데몬화
    
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
    //set_nonblocking(ssock); 
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
    room_num   = 0; //생성된 채팅룸 수
    do{
        //자식이 죽었음을 알리는 플래그
        if(child_exited_flag){
            clean_active_process();
            child_exited_flag = 0;
        }
        //클라이언트 연결 감지중 
        int csock = accept(ssock,(struct sockaddr *)&cliaddr,&cli_len);
        //set_nonblocking(csock);
        ssize_t n,client_n; 
        //파이프 관련 초기화 
        pid_t pids_; //부모 자식 구분자
        int parent_pfd[2]; //부모->자식 fd
        int child_pfd[2];  //자식->부모 fd 
        
        //디버깅용 코드
        //클라이언트의 IP 주소를 사람이 읽을 수 있는 문자열 형태로 변환하는 역할
        char check_addr[BUFSIZ];
        inet_ntop(AF_INET, &cliaddr.sin_addr,check_addr,BUFSIZ);
        syslog(LOG_INFO,"Client is connected : %s",check_addr);

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

        // 논블로킹 설정 (양쪽 다 가능)
        set_nonblocking(parent_pfd[0]); // read end
        set_nonblocking(parent_pfd[1]); // write end
        set_nonblocking(child_pfd[0]); // read end
        set_nonblocking(child_pfd[1]); // write end
        //클라이언트 서버 프로세스 생성 
        if((pids_ = fork())<0){
            syslog(LOG_ERR,"NO FORK!!");
            exit(1);
        }
        else if(pids_ == 0){ //자식 : 클라이언트에서 쓴걸 읽고 부모에게 보낸다
            //close(ssock); //자식은 클라이언트를 감지 하지 않아도 되니까 닫음
            pid_t main_pid, client_pid;
            main_pid = getppid();
            client_pid = getpid();
            // //부모는 parent_pfd[1]에 쓰고, 자식은 parent_pfd[0]에서 읽음
            // //자식은 읽어야 하니까 반대를 닫는다
            // close(child_pfd[0]);  
            // //자식은 child_pfd[1]에 쓰고, 부모는 child_pfd[0]에서 읽음
            // //자식이 써야 하니까 반대를 닫는다
            // close(parent_pfd[1]); 
            client_work(client_pid,main_pid,csock,parent_pfd,child_pfd);
        }
        else if(pids_>0){ //부모 : 자식이 보낸걸 읽고 
            close(csock); // 부모는 클라이언트 소켓을 쓰지 않으니까
            //부모는 parent_pfd[1]에 쓰고, 자식은 parent_pfd[0]에서 읽음
            //부모는 써야 하니까 반대를 닫는다
            close(parent_pfd[0]); 
            //자식은 child_pfd[1]에 쓰고, 부모는 child_pfd[0]에서 읽음
            //부모는 읽어야 하니까 반대를 닫는다
            close(child_pfd[1]);
            
            memset(&client_pipe_info[client_num], 0, sizeof(client_pipe_info[client_num]));
            client_pipe_info[client_num].pid = pids_;
            //부모가 자식에게 쓰고 있는 파이프의 단 할당
            client_pipe_info[client_num].parent_to_child_write_fd  = parent_pfd[1];
            //자식이 부모에게 온걸 읽을 수 있는 파이프의 단 할당
            client_pipe_info[client_num].child_to_parent_read_fd  = child_pfd[0];
            client_pipe_info[client_num].isActive = true;
            
            if(client_num >= MAX_CLIENT){
                syslog(LOG_ERR,"Index out of client number");
                exit(1);
            }
            //close(ssock);
            //열어놓은 파이프 닫기
            //close(parent_pfd[1]);
            //close(child_pfd[0]); 
        }
        //클라이언트의 메시지를 읽는다
        if(is_write_from_client)
        {
            is_write_from_client = 0;
            int n;
            char mesg[BUFSIZ];
            
            if((n=read(child_pfd[0],mesg,BUFSIZ)) <= 0){
                syslog(LOG_ERR,"cannot read child message");
            }
            else{
                mesg[n] = '\0';
                char *pid_str;
                char *content;

                // 첫 번째 호출: 원본 문자열과 구분자를 넘김
                pid_str = strtok(mesg, ":");  // PID 부분 추출
                pid_t from_who = atoi(pid_str);
                // 두 번째 호출: NULL과 구분자를 넘김 (내부적으로 이전 위치 기억)
                content = strtok(NULL, ":");  // 메시지 내용 부분 추출
                //명령어
                syslog(LOG_INFO,"Receive : %s",mesg);
                syslog(LOG_INFO,"CONTENT : %s",content);
                syslog(LOG_INFO,"CONTENT : %s",pid_str);
                if(content[0] == '/'){
                    int isAdd = check_command(content, "add");
                    int isJoin = check_command(content, "join");
                    int isRm = check_command(content, "rm");
                    int isList = check_command(content, "list");
                    //int isJoin = check_command(content, "join");
                    
                    if(isAdd){
                        strncpy(room_info[room_num].name, content + 2 + strlen("add"), NAME - 1);
                        room_info[room_num].name[NAME - 1] = '\0';  // 안전한 널 종료
                        room_num++;
                    }
                    else if(isJoin){
                        
                        strncpy(client_pipe_info[client_num-1].room_name, content + 2 + strlen("join"), NAME - 1);
                        client_pipe_info[client_num-1].room_name[NAME - 1] = '\0';  // 안전한 널 종료
                    }
                }
                else if(client_pipe_info[client_num-1].name[0] == '\0'){ // 클라이언트 이름
                    strncpy(client_pipe_info[client_num-1].name, content, NAME - 1);
                    client_pipe_info[client_num-1].name[NAME - 1] = '\0';
                }
                else{ // 클라이언트 채팅 뿌리기
                    //누가 보냈는지 확인하기
                    int that_room = -1;
                    char *r_name = NULL;
                    for(int i = 0; i < client_num-1; i++){
                        if(client_pipe_info[i].pid == from_who){
                            that_room = i;
                            r_name = client_pipe_info[i].room_name;
                            break;
                        }
                    }
                    if(that_room != -1){
                        for(int i = 0; i < client_num-1; i++){
                            if((strcmp(client_pipe_info[i].room_name, r_name) == 0)&& client_pipe_info[i].isActive){
                                if(write(client_pipe_info[i].parent_to_child_write_fd,content,strlen(content)+1) <= 0)
                                        syslog(LOG_ERR,"cannot Write to parent");
                                else{
                                    kill(client_pipe_info[i].pid,SIGUSR1);
                                }
                            }
                        }
                    }
                    else{
                        syslog(LOG_ERR,"NO THAT ROOM!!!");
                    }

                }
                
            }
             
        }
            
    }while(!is_shutdown);
    //열어놓은 파이프 닫기
    close(parent_pfd[1]);
    close(child_pfd[0]);
    close(parent_pfd[0]);
    close(child_pfd[1]);
    close(ssock);
    close(csock);
    return 0;
}

int check_command(const char* mesg, const char* command){
    // 1. 메시지가 '/'로 시작하는지 확인
    if (mesg[0] != '/') {
        return 0; // '/'로 시작하지 않으면 명령어가 아님
    }

    // 2. 명령어 부분과 일치하는지 확인
    // mesg + 1: '/' 다음 문자부터 비교 시작
    // strlen(command): 비교할 명령어의 길이
    if (strncmp(mesg + 1, command, strlen(command)) == 0) {
        // 3. 명령어 뒤에 공백이 오거나 문자열이 끝나는지 확인
        // (예: "/add" 뒤에 " " 또는 "\0"이 와야 함. "/addroom" 같은 경우를 걸러냄)
        char char_after_command = mesg[1 + strlen(command)];
        if (char_after_command == ' ' || char_after_command == '\0' || char_after_command == '\n') {
            return 1; // 명령어가 정확히 일치하고 뒤에 공백/종료 문자가 옴
        }
    }
    return 0; // 명령어를 찾지 못했거나 형식이 맞지 않음
}