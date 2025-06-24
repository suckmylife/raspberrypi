#include "signals.h"

void from_chatroom_mesg_handler(int signum)
{
    is_write_from_chat_room = 1;
}

void from_client_mesg_handler(int signum)
{
    is_write_from_client = 1;
}

void handler_sigchld(int signum)
{
    /*
        자식이 종료되면 감지 되겠금 하는 함수
        메인 루프에서 clean_active_process()를 또 부르고 있음
        그 이유는 
        1. 메인루프에서 하는것보다 이거 단독으로 하는게 즉각적으로
           좀비를 감지할 수 있음
        2. 메인루프에게 if문처럼 신호가 발생했다고 알려줄 수 있음
    */
    while(waitpid(-1, NULL, WNOHANG) > 0)
        child_exited_flag = 1;
}

void child_close_handler()
{
    struct sigaction sa_child;
    // SIGCHLD 핸들러 설정: 자식 프로세스의 종료를 비동기적으로 처리하여 
    // 좀비 프로세스를 방지합니다.
    sa_child.sa_handler = handler_sigchld;
    sigemptyset(&sa_child.sa_mask); // 시그널 핸들러 실행 중 블록할 시그널이 없도록 
                                   // 마스크를 비웁니다.
    sa_child.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    /*
    SA_RESTART   : waitpid()와 같은 시스템 호출이 시그널에 의해 중단될 
                   경우 자동으로 재시작
    SA_NOCLDSTOP : 자식 프로세스가 정지(STOP)되거나 계속(CONT)될 때 
                   SIGCHLD 시그널을 받지 않음
    */
    if (sigaction(SIGCHLD, &sa_child, NULL) == -1) {
        syslog(LOG_ERR, "CHILD CLOSE: Failed to set SIGCHLD handler");
        exit(1);
    }
    syslog(LOG_INFO, "CHILD CLOSE: SIGCHLD handler set for CHILD.");
}

void grace_close_handler(int signum)
{
    is_shutdown = 1;
    syslog(LOG_INFO, "Grace Shut down");
}

void setup_chatroom_handler()
{
    struct sigaction sa_chat;
    //kill로 SIGUSR1이 발생되면 이 함수가 발동되도록 등록
    sa_chat.sa_handler = from_chatroom_mesg_handler;
    //시그널 핸들러 실행 중 블록할 시그널이 없도록 마스크를 비움
    sigemptyset(&sa_chat.sa_mask);
    if (sigaction(SIG_FROM_CHATROOM, &sa_chat, NULL) == -1) {
        syslog(LOG_ERR, "Chat Room: Failed to set SIGUSR1 handler");
        exit(1);
    }
    syslog(LOG_INFO, "Chat Room: SIGUSR1 handler Sucssess.");
}

void setup_client_handler()
{
    struct sigaction sa_client;

    // SIGUSR2 핸들러 설정: 자식으로부터의 비동기 메시지 알림을 받기 위함입니다.
    sa_client.sa_handler = from_client_mesg_handler;
    sigemptyset(&sa_client.sa_mask); // 시그널 핸들러 실행 중 블록할 시그널이 없도록 
                                  // 마스크를 비웁니다.
    sa_client.sa_flags = 0; // SA_RESTART 플래그를 사용하지 않습니다. 
                         // 이는 read()나 accept() 같은 시스템 호출이 시그널에 의해 
                         // 중단될 때 자동으로 재시작되지 않고 EINTR 오류를 반환하도록 합니다.
    if (sigaction(SIG_FROM_CLIENT, &sa_client, NULL) == -1) {
        syslog(LOG_ERR, "CLIENT: Failed to set SIGUSR2 handler");
        exit(1);
    }
    syslog(LOG_INFO, "CLIENT: SIGUSR2 handler set for CLIENT.");

}

void clean_active_process()
{
    pid_t pid;
    int status;
    while((pid = waitpid(-1,&status,WNOHANG)) > 0){
        // active_children 목록에서 해당 PID를 찾아 제거하고 파이프 FD를 닫습니다.
        for (int i = 0; i < client_num; i++) {
            if (client_pipe_info[i].pid == pid) {
                // 해당 자식의 파이프 FD 닫기: 자원 누수를 방지하고, 파이프의 
                // 다른 끝에 EOF를 알립니다.
                close(client_pipe_info[i].parent_to_child_write_fd); 
                close(client_pipe_info[i].child_to_parent_read_fd);  
                
                // 배열에서 해당 자식의 정보를 제거하고 배열을 재정렬합니다.
                // 마지막 요소를 현재 위치로 이동시키고 유효한 자식 수를 감소시킵니다.
                for (int j = i; j < client_num - 1; j++) {
                    client_pipe_info[j] = client_pipe_info[j+1];
                }
                client_num--;
                syslog(LOG_INFO, "Parent: Child %d removed from list. Active children: %d.", pid, client_num);
                break; // 해당 자식을 찾았으니 루프를 종료합니다.
            }
        }

        for (int i = 0; i < room_num; i++) {
            if (chat_pipe_info[i].pid == pid) {
                // 해당 자식의 파이프 FD 닫기: 자원 누수를 방지하고, 파이프의 
                // 다른 끝에 EOF를 알립니다.
                close(chat_pipe_info[i].parent_to_child_write_fd); 
                close(chat_pipe_info[i].child_to_parent_read_fd);  
                
                // 배열에서 해당 자식의 정보를 제거하고 배열을 재정렬합니다.
                // 마지막 요소를 현재 위치로 이동시키고 유효한 자식 수를 감소시킵니다.
                for (int j = i; j < room_num - 1; j++) {
                    chat_pipe_info[j] = chat_pipe_info[j+1];
                }
                room_num--;
                syslog(LOG_INFO, "Parent: Child %d removed from list. Active children: %d.", pid, client_num);
                break; // 해당 자식을 찾았으니 루프를 종료합니다.
            }
        }
    }
}

// 소켓을 논블로킹 모드로 설정하는 함수
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        syslog(LOG_ERR,"fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1){
        syslog(LOG_ERR,"fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    return 0;
}

