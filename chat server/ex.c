#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h> // exit(), atoi() 사용
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h> // waitpid() 사용
#include <errno.h>    // errno 사용
#include <netinet/in.h> // struct sockaddr_in 사용
#include <fcntl.h>    // O_NONBLOCK 사용을 위한 fcntl()
#include <stdbool.h>  // bool 타입 사용을 위해 추가

#include "deamon.h" // 데몬화 함수가 여기에 있다고 가정
#include <syslog.h> // syslog 사용

// --- 매크로 정의 ---
#define TCP_PORT     5100
#define MAX_CLIENT   32
#define CHAT_ROOM    4
#define NAME         32

// --- 구조체 정의 ---

// 각 채팅방의 정보를 담는 구조체 (부모 프로세스에서 관리)
typedef struct {
    char name[NAME]; // 채팅방 이름
    // 여기에 채팅방을 관리하는 추가적인 정보 (예: 채팅방을 담당하는 1차 자식 PID 등)를 추가할 수 있습니다.
} roomInfo;

// 각 클라이언트 핸들링 자식 프로세스(2차 자식)의 정보를 담는 구조체 (부모 프로세스에서 관리)
typedef struct {
    pid_t pid;           // 2차 자식 프로세스의 PID
    char name[NAME];     // 클라이언트 닉네임 (입력받아 저장)
    char room_name[NAME]; // 클라이언트가 접속한 채팅방 이름
    int parent_to_child_write_fd; // 부모가 이 자식에게 메시지를 보낼 때 사용하는 파이프의 '쓰기' 끝 FD
    int child_to_parent_read_fd;  // 이 자식이 부모에게 메시지를 보낼 때, 부모가 '읽을' 파이프의 FD
    bool isActive;       // 클라이언트 연결의 활성 상태 (true: 활성, false: 비활성/종료)
} pipeInfo;

// --- 전역 변수 선언 ---
roomInfo room_info[CHAT_ROOM] = {0}; 
pipeInfo active_children[MAX_CLIENT] = {0}; 
volatile int num_active_children = 0;
volatile int room_num = 0; 

volatile sig_atomic_t parent_sigusr_arrived = 0; 
volatile sig_atomic_t child_sigusr_arrived = 0;  
volatile sig_atomic_t child_exited_flag = 0;     

// --- FCNTL 관련 함수 ---
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); 
    if (flags == -1) {
        syslog(LOG_ERR, "fcntl(F_GETFL) error for fd %d: %m", fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "fcntl(F_SETFL, O_NONBLOCK) error for fd %d: %m", fd);
        return -1;
    }
    return 0;
}

int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); 
    if (flags == -1) {
        syslog(LOG_ERR, "fcntl(F_GETFL) error for fd %d: %m", fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "fcntl(F_SETFL, blocking) error for fd %d: %m", fd);
        return -1;
    }
    return 0;
}

// --- 시그널 핸들러 함수 정의 ---
void handle_parent_sigusr(int signum) {
    parent_sigusr_arrived = 1; 
    syslog(LOG_INFO, "Parent: SIGUSR1 received (message from child).");
}

void handle_child_sigusr(int signum) {
    child_sigusr_arrived = 1;
    syslog(LOG_INFO, "Child: SIGUSR1 received (message from parent).");
}

void handle_sigchld_main(int signum) { 
    child_exited_flag = 1; 
    syslog(LOG_INFO, "Parent: SIGCHLD received. Child exited flag set.");
}

void clean_active_process() {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { 
        syslog(LOG_INFO, "Parent: Child %d terminated (status: %d).", pid, status);
        for (int i = 0; i < num_active_children; i++) {
            if (active_children[i].pid == pid) {
                close(active_children[i].parent_to_child_write_fd); 
                close(active_children[i].child_to_parent_read_fd);  
                active_children[i].isActive = false; 
                
                for (int j = i; j < num_active_children - 1; j++) {
                    active_children[j] = active_children[j+1];
                }
                num_active_children--;
                syslog(LOG_INFO, "Parent: Child %d removed from list. Active children: %d.", pid, num_active_children);
                break; 
            }
        }
    }
}

// --- 시그널 핸들러 등록 함수 ---
void setup_signal_handlers_parent_main() { 
    struct sigaction sa_usr, sa_chld;

    sa_usr.sa_handler = handle_parent_sigusr;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = 0; 
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGUSR1 handler set for parent.");

    sa_chld.sa_handler = handle_sigchld_main; 
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGCHLD handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGCHLD handler set for parent.");
}

void setup_signal_handlers_child_main() { 
    struct sigaction sa_usr;

    sa_usr.sa_handler = handle_child_sigusr;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = 0; 
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Child: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Child: SIGUSR1 handler set for child.");
}

// --- 명령어 검사 함수 ---
int check_command(const char* mesg, const char* command){
    if (mesg[0] != '/') {
        return 0;
    }
    if (strncmp(mesg + 1, command, strlen(command)) == 0) {
        char char_after_command = mesg[1 + strlen(command)];
        if (char_after_command == ' ' || char_after_command == '\0' || char_after_command == '\n') {
            return 1; 
        }
    }
    return 0;
}

// --- 클라이언트 서버 (2차 자식) 프로세스의 메인 로직 함수 ---
// 이 함수는 fork()된 자식 프로세스에서 실행됩니다.
void client_work(pid_t client_pid, pid_t main_pid, int csock, int parent_pfd[2], int child_pfd[2]) {
    // 자식 프로세스 시그널 핸들러 설정
    setup_signal_handlers_child_main(); 

    // 부모->자식 파이프 (parent_pfd):
    // 자식은 이 파이프의 '읽기 끝'(parent_pfd[0])을 사용해서 부모 메시지를 받습니다.
    // 따라서 '쓰기 끝'(parent_pfd[1])은 자식에게 불필요하므로 닫습니다.
    close(parent_pfd[1]); 

    // 자식->부모 파이프 (child_pfd):
    // 자식은 이 파이프의 '쓰기 끝'(child_pfd[1])을 사용해서 부모에게 메시지를 보냅니다.
    // 따라서 '읽기 끝'(child_pfd[0])은 자식에게 불필요하므로 닫습니다.
    close(child_pfd[0]);

    // 자식 프로세스가 실제로 통신에 사용할 파일 디스크립터들을 명확히 정의합니다.
    int client_socket_fd = csock;           // 클라이언트와의 1대1 통신 소켓
    int read_from_parent_pipe_fd = parent_pfd[0]; // 부모로부터 메시지를 읽을 파이프 FD
    int write_to_parent_pipe_fd = child_pfd[1];   // 부모에게 메시지를 쓸 파이프 FD
    
    char child_mesg_buffer[BUFSIZ]; // 자식 프로세스 내부용 메시지 버퍼
    ssize_t child_n_read_write;

    // --- 자식 프로세스의 주된 통신 루프 ---
    // 이 루프 안에서 클라이언트와 부모로부터의 메시지를 지속적으로 확인하고 처리합니다.
    // O_NONBLOCK을 사용하므로, 각 read() 호출은 블로킹되지 않고 즉시 반환하며, 데이터가 없으면 EAGAIN을 반환합니다.
    // 시그널에 의해 read()가 EINTR로 중단되면, 루프가 다시 돌면서 플래그를 확인합니다.
    while (1) {
        // 1. 부모로부터 메시지가 도착했는지 확인 (SIGUSR1 시그널 플래그 이용)
        // 부모가 SIGUSR1을 보내면 child_sigusr_arrived 플래그가 설정됩니다.
        // 이 블록은 플래그가 설정되었을 때 실행됩니다. O_NONBLOCK 설정으로 블로킹을 피합니다.
        if (child_sigusr_arrived) {
            child_sigusr_arrived = 0; // 플래그를 초기화합니다.
            syslog(LOG_INFO, "Child %d: SIGUSR1 received, checking parent pipe for broadcast.", client_pid);
            
            // 부모 파이프 FD에서 메시지를 읽기 시도: 논블로킹이므로 데이터가 없으면 즉시 반환됩니다.
            child_n_read_write = read(read_from_parent_pipe_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);
            
            if (child_n_read_write > 0) {
                child_mesg_buffer[child_n_read_write] = '\0';
                syslog(LOG_INFO, "Child %d received from parent (broadcast): %s", client_pid, child_mesg_buffer);

                // 부모로부터 받은 메시지를 해당 클라이언트에게 전달합니다.
                // 클라이언트 소켓도 논블로킹으로 설정하고 쓰기 시도합니다.
                set_nonblocking(client_socket_fd);
                if (write(client_socket_fd, child_mesg_buffer, child_n_read_write) <= 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        //syslog(LOG_ERR, "Child %d failed to write to client (broadcast): %m", client_pid);
                        break; // 쓰기 오류 시 통신 루프 종료
                    }
                }
                set_blocking(client_socket_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
            } else if (child_n_read_write == 0) {
                // 부모 파이프가 닫힘 (부모 프로세스가 종료되었거나 파이프의 모든 쓰기 끝이 닫힘)
                syslog(LOG_INFO, "Child %d: Parent pipe closed. Exiting child loop.", client_pid);
                break; // 통신 루프 종료
            } else { // child_n_read_write < 0
                // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
                // EINTR은 시그널에 의해 중단된 것이므로 실제 오류가 아닙니다.
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                    //syslog(LOG_ERR, "Child %d read from parent pipe error: %m", client_pid);
                    break; // 다른 오류 발생 시 통신 루프 종료
                }
            }
        }

        // 2. 클라이언트 소켓에서 메시지 읽기 시도 (논블로킹)
        // O_NONBLOCK 설정으로 인해 클라이언트가 데이터를 보내지 않아도 블로킹되지 않고 즉시 반환합니다.
        // 부모가 SIGUSR1 시그널을 보내면 이 read()는 EINTR 오류로 중단될 수 있습니다.
        set_nonblocking(client_socket_fd);
        child_n_read_write = read(client_socket_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);
        set_blocking(client_socket_fd); // 읽기 후 다시 블로킹 모드로 복원합니다.

        if (child_n_read_write > 0) {
            child_mesg_buffer[child_n_read_write] = '\0'; // 문자열 종료 처리
            syslog(LOG_INFO, "Child %d received from client: %s", client_pid, child_mesg_buffer);

            // 클라이언트에게 받은 메시지를 부모에게 파이프를 통해 전달합니다.
            // 메시지 형식: "PID:메시지내용" (부모가 어떤 자식에게서 왔는지 알 수 있도록)
            char formatted_mesg[BUFSIZ + 32]; // PID 공간을 고려하여 버퍼 크기 증가
            snprintf(formatted_mesg, sizeof(formatted_mesg), "%d:%s", client_pid, child_mesg_buffer);
            ssize_t formatted_len = strlen(formatted_mesg);

            // 부모에게 메시지를 보내기 전, 부모에게 SIGUSR1 시그널을 전송합니다.
            // 이렇게 해야 부모의 accept()나 다른 read()가 EINTR로 중단되어 이 메시지를 처리할 수 있습니다.
            if (kill(main_pid, SIGUSR1) == -1) { // getppid() 대신 전달받은 main_pid 사용
                syslog(LOG_ERR, "Child %d: Failed to send SIGUSR1 to parent %d: %m", client_pid, main_pid);
            } 
            // 파이프에 메시지 쓰기 시도 (O_NONBLOCK 설정으로 버퍼가 가득 차면 블로킹되지 않고 즉시 반환합니다.)
            set_nonblocking(write_to_parent_pipe_fd);
            if (write(write_to_parent_pipe_fd, formatted_mesg, formatted_len + 1) <= 0) { // NULL 종료 문자 포함
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    //syslog(LOG_ERR, "Child %d failed to write to parent pipe: %m", client_pid);
                    break; // 쓰기 오류 시 통신 루프 종료
                }
            }
            set_blocking(write_to_parent_pipe_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
        } else if (child_n_read_write == 0) {
            // 클라이언트 연결 종료 (EOF): 클라이언트가 연결을 끊었습니다.
            syslog(LOG_INFO, "Child %d: Client disconnected. Exiting child loop.", client_pid);
            break; // 통신 루프 종료
        } else { // child_n_read_write < 0
            // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
            // EINTR은 시그널에 의해 중단된 것이므로 실제 오류가 아닙니다.
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                //syslog(LOG_ERR, "Child %d read from client error: %m", client_pid);
                break; // 다른 오류 발생 시 통신 루프 종료
            }
        }
        // CPU 과부하 방지를 위해 잠시 쉬어줍니다.
        // 논블로킹 모드에서는 CPU를 계속 소모할 수 있으므로, usleep은 필수적입니다.
        usleep(10000); // 10ms (10000 microseconds)
    } // --- while (1) 루프 종료 ---

    // 자식 프로세스 종료 전 모든 열린 파일 디스크립터를 닫습니다.
    // 자원 누수를 방지하고 운영체제에 FD를 반환합니다.
    close(client_socket_fd);
    close(read_from_parent_pipe_fd);
    close(write_to_parent_pipe_fd);
    syslog(LOG_INFO, "Child %d process exiting gracefully.", client_pid);
    exit(0); // 자식 프로세스는 자신의 역할을 마치면 반드시 종료합니다.
}


int main(int argc, char **argv)
{
    int ssock;   // 서버 소켓 (클라이언트 연결을 받을 때 사용)
    int csock;   // 클라이언트 소켓 (각 클라이언트와 1대1 통신)
    socklen_t cli_len; // 주소 구조체 길이를 저장할 변수 
    struct sockaddr_in servaddr, cliaddr; // 클라이언트의 주소정보를 담을 빈 그릇
    char mesg_buffer[BUFSIZ]; // 메시지 버퍼 (main 함수용)
    ssize_t n_read_write; // 읽거나 쓴 바이트 수

    // 메인 프로세스(부모)의 시그널 핸들러를 설정합니다.
    setup_signal_handlers_parent_main(); 

    // 데몬화 함수 호출 (argc, argv 인자 전달)
    daemonize(argc, argv); 

    // 서버 소켓 생성
    if((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        syslog(LOG_ERR, "socket not create: %m");
        exit(1);
    }
    // 서버 소켓 설정
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);

    // SO_REUSEADDR 옵션 설정: 서버 재시작 시 이전에 사용 중이던 포트를 즉시 재사용할 수 있게 합니다.
    int optval = 1;
    if (setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed: %m");
        close(ssock);
        exit(1);
    }

    // 서버 소켓 연결 (바인드): 소켓에 IP 주소와 포트 번호를 할당합니다.
    if(bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        syslog(LOG_ERR, "No Bind: %m");
        exit(1);
    }
    // 서버 소켓 가동 (리스닝): 클라이언트의 연결 요청을 대기하며, 최대 8개의 대기 연결 큐를 설정합니다.
    if(listen(ssock, 8) < 0){
        syslog(LOG_ERR, "Cannot listen: %m");
        exit(1);
    }

    // 서버 소켓(ssock)을 논블로킹 모드로 설정합니다.
    // 이렇게 하면 accept() 호출 시 대기 중인 연결이 없어도 블로킹되지 않고 즉시 반환됩니다.
    if (set_nonblocking(ssock) == -1) {
        syslog(LOG_ERR, "Failed to set ssock non-blocking: %m");
        exit(1);
    }

    cli_len = sizeof(cliaddr); 
    
    // --- 부모 프로세스의 메인 루프 (새 클라이언트 연결 수락 및 자식 관리) ---
    while(true) { 
        // 자식 종료 플래그가 설정되었다면, 종료된 자식을 정리합니다.
        if(child_exited_flag){
            clean_active_process(); // SIGCHLD 핸들러가 설정한 플래그를 확인하여 실제 정리 수행
            child_exited_flag = 0; // 플래그 초기화
        }

        // 클라이언트 연결 감지 및 수락: accept()는 논블로킹 소켓이므로, 연결이 없으면 EAGAIN/EWOULDBLOCK을 반환합니다.
        // 시그널에 의해 중단되면 EINTR을 반환합니다.
        csock = accept(ssock, (struct sockaddr *)&cliaddr, &cli_len);
        if (csock < 0) {
            // accept()가 오류를 반환했을 때의 처리
            if (errno == EINTR) { 
                syslog(LOG_INFO, "Parent: accept() interrupted by signal (EINTR).");
                // 시그널에 의해 깨어났으니, 자식으로부터 온 메시지를 확인하는 로직으로 넘어갑니다.
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 논블로킹 모드에서 현재 대기 중인 연결이 없는 경우
                usleep(10000); 
                //syslog(LOG_INFO, "Parent: No pending connections, accept() returned EAGAIN/EWOULDBLOCK.");
            } else {
                syslog(LOG_ERR, "accept() error: %m");
                break; 
            }

            // 시그널에 의해 중단되었거나, 연결이 없어 EAGAIN/EWOULDBLOCK이 발생한 경우
            // 여기서 자식들로부터 온 메시지를 확인하는 로직을 수행합니다.
            if (parent_sigusr_arrived) { 
                parent_sigusr_arrived = 0; 
                syslog(LOG_INFO, "Parent: Checking for messages from children.");
                for (int i = 0; i < num_active_children; i++) {
                    if (!active_children[i].isActive) {
                        continue; 
                    }
                    n_read_write = read(active_children[i].child_to_parent_read_fd, mesg_buffer, sizeof(mesg_buffer) - 1);
                    
                    if (n_read_write > 0) {
                        mesg_buffer[n_read_write] = '\0';
                        syslog(LOG_INFO, "Parent received message from child %d: %s", active_children[i].pid, mesg_buffer);

                        char *pid_str = strtok(mesg_buffer, ":");
                        char *content = strtok(NULL, ""); 

                        if (pid_str && content) {
                            pid_t from_who = atoi(pid_str); 
                            
                            if (content[0] == '/') {
                                int isAdd = check_command(content, "add");
                                int isJoin = check_command(content, "join");
                                int isRm = check_command(content, "rm"); 
                                int isList = check_command(content, "list"); 

                                if (isAdd) {
                                    if (room_num < CHAT_ROOM) {
                                        strncpy(room_info[room_num].name, content + 2 + strlen("add"), NAME - 1);
                                        room_info[room_num].name[NAME - 1] = '\0'; 
                                        syslog(LOG_INFO, "Parent: Room '%s' created.", room_info[room_num].name);
                                        room_num++;
                                    } else {
                                        syslog(LOG_WARNING, "Parent: Max chat rooms reached. Cannot create room '%s'.", content + 2 + strlen("add"));
                                    }
                                } else if (isJoin) {
                                    char *join_room_name = content + 2 + strlen("join");
                                    int client_idx = -1;
                                    for(int k=0; k<num_active_children; k++){
                                        if(active_children[k].pid == from_who){
                                            client_idx = k;
                                            break;
                                        }
                                    }
                                    if(client_idx != -1){
                                        strncpy(active_children[client_idx].room_name, join_room_name, NAME - 1);
                                        active_children[client_idx].room_name[NAME - 1] = '\0';
                                        syslog(LOG_INFO, "Parent: Client %d ('%s') joined room '%s'.", from_who, active_children[client_idx].name, active_children[client_idx].room_name);
                                    } else {
                                        syslog(LOG_ERR, "Parent: Could not find client with PID %d to join room.", from_who);
                                    }
                                }
                            } 
                            else if (active_children[i].name[0] == '\0') { 
                                strncpy(active_children[i].name, content, NAME - 1);
                                active_children[i].name[NAME - 1] = '\0';
                                syslog(LOG_INFO, "Parent: Client %d set name to '%s'.", from_who, active_children[i].name);
                            }
                            else { 
                                char broadcast_mesg[BUFSIZ + NAME + 10]; 
                                snprintf(broadcast_mesg, sizeof(broadcast_mesg), "%s: %s", active_children[i].name, content);
                                ssize_t broadcast_len = strlen(broadcast_mesg);

                                char *sender_room_name = active_children[i].room_name;
                                if (sender_room_name[0] == '\0') { 
                                    syslog(LOG_INFO, "Parent: Message from client %d ('%s') but not in a room. Message: %s", from_who, active_children[i].name, content);
                                    continue; 
                                }

                                for (int j = 0; j < num_active_children; j++) {
                                    if (active_children[j].isActive && 
                                        (strcmp(active_children[j].room_name, sender_room_name) == 0)) 
                                    {
                                        syslog(LOG_INFO, "Parent broadcasting to client %d ('%s') in room '%s'. Message: %s", active_children[j].pid, active_children[j].name, active_children[j].room_name, broadcast_mesg);
                                        
                                        if (kill(active_children[j].pid, SIGUSR1) == -1) {
                                            syslog(LOG_ERR, "Parent: Failed to send SIGUSR1 to child %d: %m", active_children[j].pid);
                                        } else {
                                            if (write(active_children[j].parent_to_child_write_fd, broadcast_mesg, broadcast_len + 1) <= 0) { 
                                                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                                     //syslog(LOG_ERR, "Parent failed to broadcast to child %d: %m", active_children[j].pid);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            syslog(LOG_WARNING, "Parent: Received malformed message from child %d: %s", active_children[i].pid, mesg_buffer);
                        }
                    } else if (n_read_write == 0) {
                        syslog(LOG_INFO, "Parent: Child %d pipe closed (detected during read scan).", active_children[i].pid);
                    } else { 
                        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                            //syslog(LOG_ERR, "Parent read from child %d pipe error: %m", active_children[i].pid);
                        }
                    }
                }
            }
            continue; 
        }

        // --- 새로운 클라이언트 연결 처리 (csock >= 0 인 경우) ---

        if (num_active_children >= MAX_CLIENT) {
            syslog(LOG_WARNING, "MAX_CLIENT limit reached. Closing new connection from %s.", inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_buffer, BUFSIZ));
            close(csock); 
            continue; 
        }

        inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_buffer, BUFSIZ);
        syslog(LOG_INFO, "Client is connected : %s", mesg_buffer);

        int parent_pfd[2]; 
        int child_pfd[2];  

        if(pipe(child_pfd) < 0){ 
            syslog(LOG_ERR, "Failed to create child->parent pipe: %m");
            close(csock); 
            continue; 
        }
        if(pipe(parent_pfd) < 0){ 
            syslog(LOG_ERR, "Failed to create parent->child pipe: %m");
            close(child_pfd[0]); close(child_pfd[1]); 
            close(csock); 
            continue; 
        }

        if (set_nonblocking(parent_pfd[0]) == -1 || set_nonblocking(parent_pfd[1]) == -1 ||
            set_nonblocking(child_pfd[0]) == -1 || set_nonblocking(child_pfd[1]) == -1) {
            syslog(LOG_ERR, "Failed to set pipe FDs non-blocking: %m");
            close(csock);
            close(parent_pfd[0]); close(parent_pfd[1]);
            close(child_pfd[0]);  close(child_pfd[1]);
            continue;
        }

        pid_t pids_; 
        if((pids_ = fork()) < 0){ 
            syslog(LOG_ERR, "fork failed: %m");
            close(csock);
            close(parent_pfd[0]); close(parent_pfd[1]);
            close(child_pfd[0]);  close(child_pfd[1]);
            continue; 
        }
        // --- 자식 프로세스 ---
        else if(pids_ == 0){ 
            syslog(LOG_INFO, "Child process started for PID %d.", getpid());
            
            // 자식은 서버 리스닝 소켓을 사용하지 않으므로 닫습니다.
            // ssock은 main 함수의 로컬 변수지만, fork()에 의해 FD가 복제되었으므로 자식 프로세스에서 닫을 수 있습니다.
            close(ssock); 

            // client_work 함수로 제어권을 넘깁니다.
            // client_work 함수 내에서 필요한 시그널 핸들러 설정과 파이프 FD 정리가 이루어집니다.
            client_work(getpid(), getppid(), csock, parent_pfd, child_pfd);
            // client_work 내부에서 exit(0) 호출로 자식 프로세스가 종료되므로, 이 이후의 코드는 실행되지 않습니다.
        }
        // --- 부모 프로세스 ---
        else { 
            close(csock); 

            close(parent_pfd[0]); 
            close(child_pfd[1]);

            if (num_active_children < MAX_CLIENT) {
                active_children[num_active_children].pid = pids_; 
                active_children[num_active_children].parent_to_child_write_fd = parent_pfd[1]; 
                active_children[num_active_children].child_to_parent_read_fd = child_pfd[0];   
                active_children[num_active_children].isActive = true; 
                memset(active_children[num_active_children].name, 0, NAME);
                memset(active_children[num_active_children].room_name, 0, NAME);

                syslog(LOG_INFO, "Parent: Child %d added. Total active children: %d.", pids_, num_active_children + 1);
                num_active_children++; 
            } else {
                syslog(LOG_WARNING, "Parent: MAX_CLIENT limit reached. Not managing child %d.", pids_);
                close(parent_pfd[1]); 
                close(child_pfd[0]);
                kill(pids_, SIGTERM); 
            }
        }
    } 
    
    // --- 서버 종료 로직 (Graceful Shutdown) ---
    for (int i = 0; i < num_active_children; i++) {
        syslog(LOG_INFO, "Parent: Sending SIGTERM to child %d.", active_children[i].pid);
        kill(active_children[i].pid, SIGTERM);
        close(active_children[i].parent_to_child_write_fd);
        close(active_children[i].child_to_parent_read_fd);
    }
    while (wait(NULL) > 0);
    
    close(ssock); 
    syslog(LOG_INFO, "Server shutting down gracefully.");

    return 0;
}
