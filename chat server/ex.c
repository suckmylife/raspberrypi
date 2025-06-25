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

#include "deamon.h" // 데몬화 함수가 여기에 있다고 가정
#include <syslog.h> // syslog 사용

// --- 매크로 정의 ---
#define TCP_PORT     5100
#define MAX_CLIENT   32   // 세미콜론 제거
#define CHAT_ROOM    4    // 채팅방 개수
#define NAME         32   // 이름 및 방 이름 최대 길이

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
} pipeInfo; // 이름 변경: clientPipeInfo로 변경하는 것이 더 명확할 수 있습니다.

// --- 전역 변수 선언 ---
// 활성화된 클라이언트 핸들링 프로세스(2차 자식) 정보를 저장하는 배열
pipeInfo active_children[MAX_CLIENT]; 
// 현재 활성화된 클라이언트 핸들링 프로세스의 수
volatile int num_active_children = 0; // 전역 client_num을 대체하여 명확하게 사용

// 생성된 채팅룸 수 (부모 프로세스에서 관리)
volatile int room_num = 0; // 전역 room_num을 대체

// 시그널 플래그 (volatile sig_atomic_t 타입 사용)
// volatile: 컴파일러 최적화를 방지하여 시그널 핸들러와 메인 루프 간의 값 동기화를 보장합니다.
// sig_atomic_t: 시그널 핸들러 내에서 안전하게 읽고 쓸 수 있는 원자적인 타입입니다.
volatile sig_atomic_t parent_sigusr_arrived = 0; // 부모: 자식으로부터 메시지 도착 시그널 플래그
volatile sig_atomic_t child_sigusr_arrived = 0;  // 자식: 부모로부터 메시지 도착 시그널 플래그
volatile sig_atomic_t child_exited_flag = 0;     // 자식 프로세스 종료 알림 플래그 (SIGCHLD용)

// --- 함수 선언 (외부 파일에 정의되어 있다고 가정) ---
// extern void setup_client_handler();   // SIGUSR1 (자식용)
// extern void setup_chatroom_handler(); // (이름으로 유추컨대, 1차 자식(채팅방) 관련 핸들러일 수 있음)
// extern void child_close_handler();    // SIGCHLD (부모용)

// 위 함수들을 직접 여기에 정의하거나, 사용자가 제공한 common.h, signals.h, clientmanager.h에서 찾아서 사용해야 합니다.
// 여기서는 `setup_signal_handlers_parent`, `setup_signal_handlers_child`, `handle_sigchld` 등으로 직접 정의합니다.

// --- FCNTL 관련 함수 (O_NONBLOCK 설정을 위해) ---
// 파일 디스크립터를 논블로킹 모드로 설정합니다.
// 논블로킹 read/write는 데이터가 없거나 버퍼가 가득 찼을 때 블로킹되지 않고 즉시 반환합니다.
// 이를 통해 `select()` 같은 멀티플렉싱 함수 없이도 여러 FD를 빠르게 순회하며 확인할 수 있습니다.
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); // 현재 FD 플래그를 가져옵니다.
    if (flags == -1) {
        syslog(LOG_ERR, "fcntl(F_GETFL) error for fd %d: %m", fd);
        return -1;
    }
    // 현재 플래그에 O_NONBLOCK 플래그를 추가하여 논블로킹 모드로 설정합니다.
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "fcntl(F_SETFL, O_NONBLOCK) error for fd %d: %m", fd);
        return -1;
    }
    return 0;
}

// 파일 디스크립터를 블로킹 모드로 설정합니다.
int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); // 현재 FD 플래그를 가져옵니다.
    if (flags == -1) {
        syslog(LOG_ERR, "fcntl(F_GETFL) error for fd %d: %m", fd);
        return -1;
    }
    // 현재 플래그에서 O_NONBLOCK 플래그를 제거하여 블로킹 모드로 설정합니다.
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "fcntl(F_SETFL, blocking) error for fd %d: %m");
        return -1;
    }
    return 0;
}

// --- 시그널 핸들러 함수 정의 (위에서 선언한 전역 변수 사용) ---
// 부모 프로세스용 SIGUSR1 핸들러 (자식으로부터 메시지 도착 알림)
void handle_parent_sigusr(int signum) {
    parent_sigusr_arrived = 1; 
    syslog(LOG_INFO, "Parent: SIGUSR1 received (message from child).");
}

// 자식 프로세스용 SIGUSR1 핸들러 (부모로부터 메시지 도착 알림)
void handle_child_sigusr(int signum) {
    child_sigusr_arrived = 1;
    syslog(LOG_INFO, "Child: SIGUSR1 received (message from parent).");
}

// SIGCHLD 핸들러 (좀비 프로세스 방지 및 자식 목록 정리)
void handle_sigchld_main(int signum) { // 함수 이름 충돌 방지를 위해 main_ 접두사 추가
    child_exited_flag = 1; // 플래그를 설정하여 메인 루프에서 자식 정리를 유도
    syslog(LOG_INFO, "Parent: SIGCHLD received. Child exited flag set.");
}

// 자식 프로세스 정리 함수 (SIGCHLD 플래그가 설정되면 호출됨)
// 이 함수는 부모 프로세스의 메인 루프에서 주기적으로 호출되어 종료된 자식을 정리합니다.
void clean_active_process() {
    pid_t pid;
    int status;
    // WNOHANG 옵션으로 블로킹 없이 종료된 모든 자식 프로세스 정리
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { 
        syslog(LOG_INFO, "Parent: Child %d terminated (status: %d).", pid, status);
        // active_children 목록에서 해당 PID를 찾아 제거하고 파이프 FD를 닫습니다.
        for (int i = 0; i < num_active_children; i++) {
            if (active_children[i].pid == pid) {
                close(active_children[i].parent_to_child_write_fd); 
                close(active_children[i].child_to_parent_read_fd);  
                active_children[i].isActive = false; // 비활성 상태로 표시
                
                // 배열에서 해당 자식의 정보를 제거하고 배열을 재정렬합니다.
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

void setup_signal_handlers_parent_main() { // 함수 이름 충돌 방지를 위해 main_ 접두사 추가
    struct sigaction sa_usr, sa_chld;

    // SIGUSR1 핸들러 설정
    sa_usr.sa_handler = handle_parent_sigusr;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = 0; 
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGUSR1 handler set for parent.");

    // SIGCHLD 핸들러 설정
    sa_chld.sa_handler = handle_sigchld_main; // clean_active_process를 직접 호출하는 대신 플래그 설정
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGCHLD handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGCHLD handler set for parent.");
}

void setup_signal_handlers_child_main() { // 함수 이름 충돌 방지를 위해 main_ 접두사 추가
    struct sigaction sa_usr;

    // SIGUSR1 핸들러 설정
    sa_usr.sa_handler = handle_child_sigusr;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = 0; 
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Child: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Child: SIGUSR1 handler set for child.");
}

// --- 명령어 검사 함수 (기존 로직 유지) ---
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
    setup_signal_handlers_child_main(); // main_ 접두사 추가

    // 자식은 서버 리스닝 소켓을 사용하지 않으므로 닫습니다.
    close(main_pid); // 부모의 PID는 main_pid에 저장될 수 있지만, ssock을 닫아야 합니다.
                     // 여기서 main_pid는 실제 FD가 아니므로, close(ssock)을 직접 호출해야 합니다.
                     // (ssock은 fork()로 복제된 FD이므로 자식도 접근 가능)
    // ssock은 client_work 함수로 전달되지 않으므로, 이 함수 안에서 닫을 수 없습니다.
    // main 함수에서 자식 프로세스 진입 직후 close(ssock); 를 해야 합니다.

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
            
            // 부모 파이프 FD를 논블로킹으로 설정하고 읽기 시도합니다.
            // 데이터가 없으면 EAGAIN/EWOULDBLOCK을 반환하므로, 블로킹되지 않고 다음 코드로 진행합니다.
            child_n_read_write = read(read_from_parent_pipe_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);
            
            if (child_n_read_write > 0) {
                child_mesg_buffer[child_n_read_write] = '\0';
                syslog(LOG_INFO, "Child %d received from parent (broadcast): %s", client_pid, child_mesg_buffer);

                // 부모로부터 받은 메시지를 해당 클라이언트에게 전달합니다.
                // 클라이언트 소켓도 논블로킹으로 설정하고 쓰기 시도합니다.
                set_nonblocking(client_socket_fd);
                if (write(client_socket_fd, child_mesg_buffer, child_n_read_write) <= 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        syslog(LOG_ERR, "Child %d failed to write to client (broadcast): %m", client_pid);
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
                    syslog(LOG_ERR, "Child %d read from parent pipe error: %m", client_pid);
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
                    syslog(LOG_ERR, "Child %d failed to write to parent pipe: %m", client_pid);
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
            // EINTR은 시그널에 의한 중단이므로 실제 오류가 아닙니다.
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                syslog(LOG_ERR, "Child %d read from client error: %m", client_pid);
                break; // 다른 오류 발생 시 통신 루프 종료
            }
        }
        // CPU 과부하 방지를 위해 잠시 쉬어줍니다. (너무 짧으면 busy-waiting 발생)
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

    // room_info와 client_pipe_info는 main 함수 내에서 사용되므로,
    // 전역 변수 active_children을 사용하는 것으로 통일합니다.
    // roomInfo room_info[CHAT_ROOM] = {0}; // 이 변수는 현재 사용되지 않으므로 주석 처리하거나 제거
    // pipeInfo client_pipe_info[CHAT_ROOM] = {0}; // active_children 배열이 이 역할을 대신함.

    // 시그널 핸들러 등록 (사용자가 제공한 함수 이름에 맞춤)
    // setup_client_handler(); // SIGUSR1 자식용 핸들러 (client_work에서 호출)
    // setup_chatroom_handler(); // 채팅룸 관련 시그널 (현재 코드에서 명확치 않음)
    // child_close_handler(); // SIGCHLD 부모용 핸들러 (main에서 호출)

    // 메인 프로세스(부모)의 시그널 핸들러를 설정합니다.
    setup_signal_handlers_parent_main(); 

    daemonize(argc, argv); // 데몬화

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
    
    // client_num 대신 전역 num_active_children 사용
    // room_num은 채팅방 관리 기능에 따라 추후 사용될 수 있습니다.

    // --- 부모 프로세스의 메인 루프 (새 클라이언트 연결 수락 및 자식 관리) ---
    while(1) { // 서버는 클라이언트 연결을 계속 받아야 하므로 무한 루프입니다.
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
            if (errno == EINTR) { // 시그널에 의해 accept()가 중단된 경우
                syslog(LOG_INFO, "Parent: accept() interrupted by signal (EINTR).");
                // 시그널에 의해 깨어났으니, 자식으로부터 온 메시지를 확인하는 로직으로 넘어갑니다.
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 논블로킹 모드에서 현재 대기 중인 연결이 없는 경우
                // 이는 오류가 아니므로, 잠시 쉬어주고 (CPU 낭비 방지) 루프를 계속합니다.
                usleep(10000); // 10ms (10000 microseconds) 쉬어주어 CPU 과부하 방지
                syslog(LOG_INFO, "Parent: No pending connections, accept() returned EAGAIN/EWOULDBLOCK.");
            } else {
                // 다른 심각한 accept() 오류 발생 시 서버를 종료합니다.
                syslog(LOG_ERR, "accept() error: %m");
                break; // while(1) 루프 종료
            }

            // 시그널에 의해 중단되었거나, 연결이 없어 EAGAIN/EWOULDBLOCK이 발생한 경우
            // 여기서 자식들로부터 온 메시지를 확인하는 로직을 수행합니다.
            if (parent_sigusr_arrived) { // SIGUSR1 플래그가 설정되어 있다면 (자식이 메시지 보냈음을 알림)
                parent_sigusr_arrived = 0; // 플래그를 초기화합니다.
                syslog(LOG_INFO, "Parent: Checking for messages from children.");
                // 모든 활성 자식의 파이프를 순회하며 메시지를 확인하고 브로드캐스트합니다.
                // 각 파이프 FD는 이미 논블로킹으로 설정되어 있으므로, read()는 블로킹되지 않습니다.
                for (int i = 0; i < num_active_children; i++) {
                    n_read_write = read(active_children[i].child_to_parent_read_fd, mesg_buffer, sizeof(mesg_buffer) - 1);
                    
                    if (n_read_write > 0) {
                        mesg_buffer[n_read_write] = '\0';
                        syslog(LOG_INFO, "Parent received message from child %d: %s", active_children[i].pid, mesg_buffer);

                        // 메시지 파싱 (PID와 내용 분리)
                        char *pid_str = strtok(mesg_buffer, ":");
                        char *content = strtok(NULL, ""); // 나머지 전체를 내용으로

                        if (pid_str && content) {
                            pid_t from_who = atoi(pid_str); // 메시지를 보낸 자식의 PID
                            
                            // 명령어 처리
                            if (content[0] == '/') {
                                int isAdd = check_command(content, "add");
                                int isJoin = check_command(content, "join");
                                int isRm = check_command(content, "rm");
                                int isList = check_command(content, "list"); // /list 구현 필요

                                if (isAdd) {
                                    // 채팅방 생성 로직 (현재 room_info[room_num] 사용)
                                    // 실제로는 1차 자식 프로세스(채팅방 서버)를 fork하고 해당 방을 활성화해야 함
                                    if (room_num < CHAT_ROOM) {
                                        strncpy(room_info[room_num].name, content + 2 + strlen("add"), NAME - 1);
                                        room_info[room_num].name[NAME - 1] = '\0'; 
                                        syslog(LOG_INFO, "Parent: Room '%s' created.", room_info[room_num].name);
                                        room_num++;
                                    } else {
                                        syslog(LOG_WARNING, "Parent: Max chat rooms reached.");
                                        // 클라이언트에게 방 생성 실패 메시지 전달 필요 (해당 자식에게만 write)
                                    }
                                } else if (isJoin) {
                                    // 클라이언트를 특정 방에 조인시키는 로직
                                    // 어떤 클라이언트(from_who)가 어떤 방(content)에 조인했는지 active_children에 업데이트
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
                                    }
                                }
                                // /rm, /list, /users 등 다른 명령어 처리
                            } 
                            // 클라이언트 이름 설정 로직
                            else if (active_children[i].name[0] == '\0') { 
                                strncpy(active_children[i].name, content, NAME - 1);
                                active_children[i].name[NAME - 1] = '\0';
                                syslog(LOG_INFO, "Parent: Client %d set name to '%s'.", from_who, active_children[i].name);
                            }
                            // 일반 채팅 메시지 브로드캐스트
                            else { 
                                char broadcast_mesg[BUFSIZ + NAME + 10]; // "이름: 메시지" 형식
                                snprintf(broadcast_mesg, sizeof(broadcast_mesg), "%s: %s", active_children[i].name, content);
                                ssize_t broadcast_len = strlen(broadcast_mesg);

                                // 메시지를 보낸 클라이언트가 속한 방을 찾습니다.
                                char *sender_room_name = active_children[i].room_name;
                                if (sender_room_name[0] == '\0') { // 아직 방에 조인하지 않은 클라이언트
                                    syslog(LOG_INFO, "Parent: Message from client %d ('%s') but not in a room.", from_who, active_children[i].name);
                                    // 클라이언트에게 "방에 먼저 조인하세요" 메시지 전달 (선택 사항)
                                    continue; // 브로드캐스트하지 않음
                                }

                                // 동일한 방에 있는 모든 활성 자식들에게 메시지를 브로드캐스트합니다.
                                for (int j = 0; j < num_active_children; j++) {
                                    // 활성 상태이고, 메시지 보낸 클라이언트 자신이 아니며, 동일한 방에 속해 있는 경우
                                    if (active_children[j].isActive && 
                                        (strcmp(active_children[j].room_name, sender_room_name) == 0)) 
                                    {
                                        syslog(LOG_INFO, "Parent broadcasting to client %d ('%s') in room '%s'.", active_children[j].pid, active_children[j].name, active_children[j].room_name);
                                        
                                        // 자식에게 메시지를 보내기 전, 해당 자식에게 SIGUSR1 시그널을 전송합니다.
                                        // 이렇게 해야 자식의 블로킹 read()가 EINTR로 중단되어 부모의 메시지를 처리할 수 있습니다.
                                        if (kill(active_children[j].pid, SIGUSR1) == -1) {
                                            syslog(LOG_ERR, "Parent: Failed to send SIGUSR1 to child %d: %m", active_children[j].pid);
                                        } else {
                                            // 파이프에 메시지 쓰기 시도. 파이프 FD는 논블로킹으로 설정되어 있습니다.
                                            if (write(active_children[j].parent_to_child_write_fd, broadcast_mesg, broadcast_len + 1) <= 0) { // NULL 종료 문자 포함
                                                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                                     syslog(LOG_ERR, "Parent failed to broadcast to child %d: %m", active_children[j].pid);
                                                     // 쓰기 실패 처리 (예: 해당 자식 연결 끊김으로 간주)
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
                        // 자식 파이프가 닫힘 (해당 자식이 종료됨을 의미)
                        // 이 자식은 SIGCHLD 핸들러에서 정리될 것이므로 여기서는 추가 처리하지 않습니다.
                        syslog(LOG_INFO, "Parent: Child %d pipe closed (detected during read scan).", active_children[i].pid);
                    } else { // n_read_write < 0
                        // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
                        // EINTR은 시그널에 의해 중단된 경우이므로 실제 오류로 간주하지 않고 루프를 계속합니다.
                        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                            syslog(LOG_ERR, "Parent read from child %d pipe error: %m", active_children[i].pid);
                        }
                    }
                }
            }
            // 시그널 처리 후 다음 accept() 시도로 돌아가기 위해 continue
            continue; 
        }

        // 클라이언트가 MAX_CLIENT를 초과하면 새로운 연결을 받아들이지 않고 닫습니다.
        if (num_active_children >= MAX_CLIENT) {
            syslog(LOG_WARNING, "MAX_CLIENT limit reached. Closing new connection from %s.", inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_buffer, BUFSIZ));
            close(csock); // 새 연결 소켓 닫기
            continue; // 다음 accept() 시도로 돌아갑니다.
        }

        // 클라이언트의 IP 주소를 사람이 읽을 수 있는 문자열 형태로 변환하고 로깅합니다.
        inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_buffer, BUFSIZ);
        syslog(LOG_INFO, "Client is connected : %s", mesg_buffer);

        // 새 클라이언트 연결을 위한 파이프 생성을 위한 임시 배열 선언
        int parent_pfd[2]; // 부모->자식 통신용 파이프 FD를 담을 배열 (부모가 쓰기, 자식이 읽기)
        int child_pfd[2];  // 자식->부모 통신용 파이프 FD를 담을 배열 (자식이 쓰기, 부모가 읽기)

        // 자식 --> 부모 파이프 생성: 이 파이프는 자식 프로세스가 부모에게 메시지를 보낼 때 사용됩니다.
        if(pipe(child_pfd) < 0){ 
            syslog(LOG_ERR, "Failed to create child->parent pipe: %m");
            close(csock); // 파이프 생성 실패 시 클라이언트 소켓 닫기
            continue; // 다음 클라이언트 연결을 시도합니다.
        }
        // 부모 --> 자식 파이프 생성: 이 파이프는 부모 프로세스가 자식에게 메시지를 보낼 때 사용됩니다.
        if(pipe(parent_pfd) < 0){ 
            syslog(LOG_ERR, "Failed to create parent->child pipe: %m");
            close(child_pfd[0]); close(child_pfd[1]); // 위에서 만든 파이프 FD도 닫기
            close(csock); // 클라이언트 소켓 닫기
            continue; // 다음 클라이언트 연결을 시도합니다.
        }

        // 새로 생성된 파이프 FD들을 논블로킹 모드로 설정합니다.
        // 이렇게 하면 read/write 시 데이터가 없거나 버퍼가 가득 찼을 때 블로킹되지 않고 즉시 반환됩니다.
        if (set_nonblocking(parent_pfd[0]) == -1 || set_nonblocking(parent_pfd[1]) == -1 ||
            set_nonblocking(child_pfd[0]) == -1 || set_nonblocking(child_pfd[1]) == -1) {
            syslog(LOG_ERR, "Failed to set pipe FDs non-blocking: %m");
            close(csock);
            close(parent_pfd[0]); close(parent_pfd[1]);
            close(child_pfd[0]);  close(child_pfd[1]);
            continue;
        }

        pid_t pids_; 
        if((pids_ = fork()) < 0){ // fork() 호출: 새 프로세스를 생성합니다.
            syslog(LOG_ERR, "fork failed: %m");
            // fork 실패 시 열린 모든 리소스(소켓, 파이프)를 닫아 자원 누수를 방지합니다.
            close(csock);
            close(parent_pfd[0]); close(parent_pfd[1]);
            close(child_pfd[0]);  close(child_pfd[1]);
            continue; // 다음 클라이언트 연결을 시도합니다.
        }
        // --- 자식 프로세스 ---
        else if(pids_ == 0){ // 자식 프로세스는 fork()의 반환 값이 0입니다.
            syslog(LOG_INFO, "Child process started for PID %d.", getpid());
            setup_signal_handlers_child_main(); // 자식 프로세스에 시그널 핸들러를 설정합니다.

            // 자식은 서버 리스닝 소켓을 사용하지 않으므로 닫습니다. (부모만 사용)
            close(ssock);

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
                    syslog(LOG_INFO, "Child %d: SIGUSR1 received, checking parent pipe for broadcast.", getpid());
                    
                    // 부모 파이프 FD에서 메시지를 읽기 시도: 논블로킹이므로 데이터가 없으면 즉시 반환됩니다.
                    child_n_read_write = read(read_from_parent_pipe_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);
                    
                    if (child_n_read_write > 0) {
                        child_mesg_buffer[child_n_read_write] = '\0';
                        syslog(LOG_INFO, "Child %d received from parent (broadcast): %s", getpid(), child_mesg_buffer);

                        // 부모로부터 받은 메시지를 해당 클라이언트에게 전달합니다.
                        // 클라이언트 소켓도 논블로킹으로 설정하고 쓰기 시도합니다.
                        set_nonblocking(client_socket_fd);
                        if (write(client_socket_fd, child_mesg_buffer, child_n_read_write) <= 0) {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                syslog(LOG_ERR, "Child %d failed to write to client (broadcast): %m", getpid());
                                break; // 쓰기 오류 시 통신 루프 종료
                            }
                        }
                        set_blocking(client_socket_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
                    } else if (child_n_read_write == 0) {
                        // 부모 파이프가 닫힘 (부모 프로세스가 종료되었거나 파이프의 모든 쓰기 끝이 닫힘)
                        syslog(LOG_INFO, "Child %d: Parent pipe closed. Exiting child loop.", getpid());
                        break; // 통신 루프 종료
                    } else { // child_n_read_write < 0
                        // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
                        // EINTR은 시그널에 의해 중단된 것이므로 실제 오류가 아닙니다.
                        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                            syslog(LOG_ERR, "Child %d read from parent pipe error: %m", getpid());
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
                    syslog(LOG_INFO, "Child %d received from client: %s", getpid(), child_mesg_buffer);

                    // 클라이언트에게 받은 메시지를 부모에게 파이프를 통해 전달합니다.
                    // 메시지 형식: "PID:메시지내용" (부모가 어떤 자식에게서 왔는지 알 수 있도록)
                    char formatted_mesg[BUFSIZ + 32]; // PID 공간을 고려하여 버퍼 크기 증가
                    snprintf(formatted_mesg, sizeof(formatted_mesg), "%d:%s", getpid(), child_mesg_buffer);
                    ssize_t formatted_len = strlen(formatted_mesg);

                    // 부모에게 메시지를 보내기 전, 부모에게 SIGUSR1 시그널을 전송합니다.
                    // 이렇게 해야 부모의 accept()나 다른 read()가 EINTR로 중단되어 이 메시지를 처리할 수 있습니다.
                    if (kill(getppid(), SIGUSR1) == -1) { // getppid()로 부모 PID를 얻습니다.
                        syslog(LOG_ERR, "Child %d: Failed to send SIGUSR1 to parent %d: %m", getpid(), getppid());
                    } 
                    // 파이프에 메시지 쓰기 시도 (O_NONBLOCK 설정으로 버퍼가 가득 차면 블로킹되지 않고 즉시 반환합니다.)
                    set_nonblocking(write_to_parent_pipe_fd);
                    if (write(write_to_parent_pipe_fd, formatted_mesg, formatted_len + 1) <= 0) { // NULL 종료 문자 포함
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            syslog(LOG_ERR, "Child %d failed to write to parent pipe: %m", getpid());
                            break; // 쓰기 오류 시 통신 루프 종료
                        }
                    }
                    set_blocking(write_to_parent_pipe_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
                } else if (child_n_read_write == 0) {
                    // 클라이언트 연결 종료 (EOF): 클라이언트가 연결을 끊었습니다.
                    syslog(LOG_INFO, "Child %d: Client disconnected. Exiting child loop.", getpid());
                    break; // 통신 루프 종료
                } else { // child_n_read_write < 0
                    // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
                    // EINTR은 시그널에 의한 중단이므로 실제 오류가 아닙니다.
                    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                        syslog(LOG_ERR, "Child %d read from client error: %m", getpid());
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
            syslog(LOG_INFO, "Child %d process exiting gracefully.", getpid());
            exit(0); // 자식 프로세스는 자신의 역할을 마치면 반드시 종료합니다.
        }
        // --- 부모 프로세스 ---
        else { // 부모 프로세스 (pids_는 새로 생성된 자식의 PID를 가집니다.)
            // 클라이언트 소켓 닫기: 부모는 클라이언트와 직접 통신하지 않고, 자식에게 소켓을 넘겨주었으므로 닫습니다.
            close(csock); 

            // 부모에게 불필요한 파이프 FD 닫기:
            // 부모 -> 자식 파이프 (parent_pfd): 부모는 '쓰기'(parent_pfd[1])만 사용하므로 '읽기 끝'(parent_pfd[0])은 닫습니다.
            close(parent_pfd[0]); 
            // 자식 -> 부모 파이프 (child_pfd): 부모는 '읽기'(child_pfd[0])만 사용하므로 '쓰기 끝'(child_pfd[1])은 닫습니다.
            close(child_pfd[1]);

            // pipeInfo 구조체에 자식 정보와 파이프 FD를 저장합니다.
            // MAX_CLIENT 제한을 확인하여 배열 오버플로우를 방지하고, 새로운 자식을 관리합니다.
            if (num_active_children < MAX_CLIENT) {
                active_children[num_active_children].pid = pids_; 
                active_children[num_active_children].parent_to_child_write_fd = parent_pfd[1]; // 부모가 자식에게 쓸 FD
                active_children[num_active_children].child_to_parent_read_fd = child_pfd[0];   // 부모가 자식에게서 읽을 FD
                active_children[num_active_children].isActive = true; // 클라이언트 활성 상태 표시
                // 클라이언트 닉네임과 방 이름 초기화
                memset(active_children[num_active_children].name, 0, NAME);
                memset(active_children[num_active_children].room_name, 0, NAME);

                syslog(LOG_INFO, "Parent: Child %d added. Total active children: %d.", pids_, num_active_children + 1);
                num_active_children++; // 활성화된 자식 수 증가
            } else {
                syslog(LOG_WARNING, "Parent: MAX_CLIENT limit reached. Not managing child %d.", pids_);
                // 최대 클라이언트 수 초과 시, 이미 생성된 파이프 FD를 닫고 자식에게 종료 시그널을 보냅니다.
                // 이렇게 하면 자식 프로세스가 불필요하게 실행되지 않도록 합니다.
                close(parent_pfd[1]); 
                close(child_pfd[0]);
                kill(pids_, SIGTERM); // 자식 프로세스를 종료하도록 요청합니다.
            }
            
            // waitpid()는 SIGCHLD 핸들러에서 비동기적으로 처리되어야 합니다.
            // 이곳에 두면 accept()가 블로킹되어 다른 클라이언트 연결을 받지 못합니다.
        }
    } // --- while(1) (accept) 루프 종료 ---
    
    // --- 서버 종료 로직 (Graceful Shutdown) ---
    // 이 부분은 Ctrl+C (SIGINT)나 kill 명령 (SIGTERM)과 같은 외부 시그널 핸들러에서 호출될 때
    // 서버를 안전하게 종료하기 위한 코드입니다.
    // main 루프가 정상적으로 종료되는 경우는 거의 없으므로, 이 코드가 직접 실행될 가능성은 낮습니다.
    
    // 모든 활성 자식 프로세스에게 종료 시그널(SIGTERM)을 보내어 종료를 요청합니다.
    for (int i = 0; i < num_active_children; i++) {
        syslog(LOG_INFO, "Parent: Sending SIGTERM to child %d.", active_children[i].pid);
        kill(active_children[i].pid, SIGTERM);
        // 자식 파이프 FD를 닫아 자식 프로세스가 파이프 EOF를 받도록 유도할 수도 있습니다.
        close(active_children[i].parent_to_child_write_fd);
        close(active_children[i].child_to_parent_read_fd);
    }
    // 남아있는 모든 자식 프로세스가 실제로 종료될 때까지 대기하여 좀비화를 방지합니다.
    // (SIGCHLD 핸들러가 주된 역할을 하지만, 안전을 위해 추가적으로 대기합니다.)
    while (wait(NULL) > 0);
    
    close(ssock); // 서버 리스닝 소켓을 닫습니다.
    syslog(LOG_INFO, "Server shutting down gracefully.");

    return 0;
}
