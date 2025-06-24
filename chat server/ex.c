#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h> // errno 사용을 위해 추가
#include <fcntl.h> // fcntl 사용을 위해 O_NONBLOCK 관련 함수를 다시 포함

#include "deamon.h" // 데몬화 함수가 여기에 있다고 가정
#include <syslog.h> // syslog 사용을 위해 추가

#define TCP_PORT     5100
#define MAX_CLIENT   32 // 세미콜론 제거

// 각 자식 프로세스(클라이언트 핸들러)의 정보를 담는 구조체
typedef struct {
    // 자식 프로세스의 PID
    pid_t pid;           
    // 부모가 이 자식에게 메시지를 보낼 때 사용하는 파이프의 '쓰기' 끝 FD (부모 관점)
    int parent_to_child_write_fd;
    // 이 자식이 부모에게 메시지를 보낼 때, 부모가 '읽을' 파이프의 FD (부모 관점)
    int child_to_parent_read_fd;
} pipeInfo;

// --- 전역 변수 선언 ---
// 활성화된 자식 프로세스 정보를 저장하는 배열
pipeInfo active_children[MAX_CLIENT]; 
// 현재 활성화된 자식 프로세스의 수
volatile int num_active_children = 0;

// 시그널 플래그 (volatile sig_atomic_t 타입 사용)
// volatile 키워드는 컴파일러가 이 변수를 최적화하지 않고, 매번 메모리에서 최신 값을 읽어오도록 지시합니다.
// 시그널 핸들러와 메인 루프처럼 비동기적으로 접근하는 경우, 이 플래그가 정확히 동기화되도록 보장합니다.
// sig_atomic_t는 시그널 핸들러 내에서 안전하게 읽고 쓸 수 있는 원자적인(atomic) 타입임을 나타냅니다.
volatile sig_atomic_t parent_sigusr_arrived = 0; 
volatile sig_atomic_t child_sigusr_arrived = 0; 


// --- 시그널 핸들러 함수 정의 ---

// 부모 프로세스용 SIGUSR1 핸들러: 자식으로부터 메시지가 도착했음을 알림
void handle_parent_sigusr(int signum) {
    parent_sigusr_arrived = 1; // 플래그를 설정하여 메인 루프에 메시지 도착을 알립니다.
    syslog(LOG_INFO, "Parent: SIGUSR1 received (message from child).");
}

// 자식 프로세스용 SIGUSR1 핸들러: 부모로부터 메시지가 도착했음을 알림
void handle_child_sigusr(int signum) {
    child_sigusr_arrived = 1; // 플래그를 설정하여 메인 루프에 메시지 도착을 알립니다.
    syslog(LOG_INFO, "Child: SIGUSR1 received (message from parent).");
}

// SIGCHLD 핸들러 (좀비 프로세스 방지 및 자식 목록 정리)
// 자식 프로세스가 종료될 때 부모에게 전송되는 시그널을 처리합니다.
void handle_sigchld(int signum) {
    pid_t pid;
    int status;
    // WNOHANG 옵션으로 블로킹 없이 종료된 모든 자식 프로세스 정리
    // waitpid(-1, ...)는 어떤 자식이라도, WNOHANG은 블로킹하지 않고 즉시 반환하며,
    // > 0 인 동안 반복하여 모든 좀비 자식이 정리될 때까지 처리합니다.
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { 
        syslog(LOG_INFO, "Parent: Child %d terminated (status: %d).", pid, status);
        // active_children 목록에서 해당 PID를 찾아 제거하고 파이프 FD를 닫습니다.
        for (int i = 0; i < num_active_children; i++) {
            if (active_children[i].pid == pid) {
                // 해당 자식의 파이프 FD 닫기: 자원 누수를 방지하고, 파이프의 
                // 다른 끝에 EOF를 알립니다.
                close(active_children[i].parent_to_child_write_fd); 
                close(active_children[i].child_to_parent_read_fd);  
                
                // 배열에서 해당 자식의 정보를 제거하고 배열을 재정렬합니다.
                // 마지막 요소를 현재 위치로 이동시키고 유효한 자식 수를 감소시킵니다.
                for (int j = i; j < num_active_children - 1; j++) {
                    active_children[j] = active_children[j+1];
                }
                num_active_children--;
                syslog(LOG_INFO, "Parent: Child %d removed from list. Active children: %d.", pid, num_active_children);
                break; // 해당 자식을 찾았으니 루프를 종료합니다.
            }
        }
    }
}

// --- 시그널 핸들러 등록 함수 ---

// 부모 프로세스에 필요한 시그널 핸들러들을 등록합니다.
void setup_signal_handlers_parent() {
    struct sigaction sa_usr, sa_chld;

    // SIGUSR1 핸들러 설정: 자식으로부터의 비동기 메시지 알림을 받기 위함입니다.
    sa_usr.sa_handler = handle_parent_sigusr;
    sigemptyset(&sa_usr.sa_mask); // 시그널 핸들러 실행 중 블록할 시그널이 없도록 
                                  // 마스크를 비웁니다.
    sa_usr.sa_flags = 0; // SA_RESTART 플래그를 사용하지 않습니다. 
                         // 이는 read()나 accept() 같은 시스템 호출이 시그널에 의해 
                         // 중단될 때 자동으로 재시작되지 않고 EINTR 오류를 반환하도록 합니다.
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGUSR1 handler set for parent.");

    // SIGCHLD 핸들러 설정: 자식 프로세스의 종료를 비동기적으로 처리하여 
    // 좀비 프로세스를 방지합니다.
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask); // 시그널 핸들러 실행 중 블록할 시그널이 없도록 
                                   // 마스크를 비웁니다.
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; // SA_RESTART: waitpid()와 같은 
                                                  // 시스템 호출이 시그널에 의해 중단될 
                                                  // 경우 자동으로 재시작됩니다.
                                                  // SA_NOCLDSTOP: 자식 프로세스가 
                                                  // 정지(STOP)되거나 계속(CONT)될 때 
                                                  // SIGCHLD 시그널을 받지 않도록 합니다.
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGCHLD handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGCHLD handler set for parent.");
}

// 자식 프로세스에 필요한 시그널 핸들러를 등록합니다.
void setup_signal_handlers_child() {
    struct sigaction sa_usr;

    // SIGUSR1 핸들러 설정: 부모로부터의 비동기 메시지 알림을 받기 위함입니다.
    sa_usr.sa_handler = handle_child_sigusr;
    sigemptyset(&sa_usr.sa_mask); // 시그널 핸들러 실행 중 블록할 시그널이 없도록 마스크를 비웁니다.
    sa_usr.sa_flags = 0; // SA_RESTART 플래그를 사용하지 않아 read()가 시그널에 의해 중단 시 EINTR을 반환하도록 합니다.
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Child: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Child: SIGUSR1 handler set for child.");
}

// --- FCNTL 관련 함수 (O_NONBLOCK 설정을 위해 다시 포함) ---
// O_NONBLOCK은 read/write 호출이 데이터가 없거나 버퍼가 가득 찼을 때 블로킹되지 않고 즉시 반환하게 합니다.
// 이를 통해 시그널 없이도 여러 FD를 순회하며 확인할 수 있으나,
// 과제 지침 (select/poll 등 I/O 멀티플렉싱 함수 사용 금지)에 따라 간접적인 폴링으로 간주될 수 있으므로
// 이 사용 여부는 교수님의 정확한 의도를 확인하는 것이 중요합니다.
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

int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); // 현재 FD 플래그를 가져옵니다.
    if (flags == -1) {
        syslog(LOG_ERR, "fcntl(F_GETFL) error for fd %d: %m", fd);
        return -1;
    }
    // 현재 플래그에서 O_NONBLOCK 플래그를 제거하여 블로킹 모드로 설정합니다.
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "fcntl(F_SETFL, blocking) error for fd %d: %m", fd);
        return -1;
    }
    return 0;
}


int main(int argc, char **argv)
{
    int ssock;   // 서버 소켓 (클라이언트 연결을 받을 때 사용)
    int csock;   // 클라이언트 소켓 (각 클라이언트와 1대1 통신)
    socklen_t clen; // 주소 구조체 길이를 저장할 변수 
    struct sockaddr_in servaddr, cliaddr; // 클라이언트의 주소정보를 담을 빈 그릇
    char mesg_buffer[BUFSIZ]; // 메시지 버퍼 (main 함수용)
    ssize_t n_read_write; // 읽거나 쓴 바이트 수

    daemonize(argv); // 데몬화

    // 부모 프로세스의 시그널 핸들러 설정은 main 함수 초기에 한 번만 호출합니다.
    setup_signal_handlers_parent(); 

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
    // 이는 서버가 비정상적으로 종료된 후 빠르게 다시 시작할 때 "Address already in use" 오류를 방지하는 데 유용합니다.
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

    clen = sizeof(cliaddr); 

    // --- 부모 프로세스의 메인 루프 (새 클라이언트 연결 수락 및 자식 관리) ---
    while(1) { // 서버는 클라이언트 연결을 계속 받아야 하므로 무한 루프입니다.
        // 클라이언트 연결 감지 및 수락: accept()는 새 연결이 올 때까지 블로킹됩니다.
        // 자식으로부터 SIGUSR1 시그널을 받으면, accept()가 EINTR 오류를 반환하며 중단될 수 있습니다.
        csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);
        if (csock < 0) {
            if (errno == EINTR) { // accept()가 시그널에 의해 중단된 경우
                syslog(LOG_INFO, "Parent: accept() interrupted by signal (EINTR).");
                // 시그널이 왔으므로, 자식으로부터 온 메시지를 확인하는 로직을 실행합니다.
                if (parent_sigusr_arrived) { // SIGUSR1 플래그가 설정되어 있다면
                    parent_sigusr_arrived = 0; // 플래그를 초기화합니다.
                    syslog(LOG_INFO, "Parent: Checking for messages from children.");
                    // 모든 활성 자식의 파이프를 순회하며 메시지를 확인하고 브로드캐스트합니다.
                    // O_NONBLOCK을 사용하므로, read() 호출은 데이터가 없으면 즉시 EAGAIN/EWOULDBLOCK을 반환합니다.
                    // 이를 통해 부모는 블로킹되지 않고 모든 자식 파이프를 빠르게 확인할 수 있습니다.
                    for (int i = 0; i < num_active_children; i++) {
                        // 자식으로부터 읽을 파이프 FD를 논블로킹으로 설정합니다.
                        // O_NONBLOCK을 사용함으로써, read() 호출이 블로킹되지 않고 즉시 반환될 수 있습니다.
                        set_nonblocking(active_children[i].child_to_parent_read_fd);
                        n_read_write = read(active_children[i].child_to_parent_read_fd, mesg_buffer, sizeof(mesg_buffer) - 1);
                        set_blocking(active_children[i].child_to_parent_read_fd); // 읽기 후에는 다시 블로킹 모드로 복원하여 다음 블로킹 read를 준비합니다.

                        if (n_read_write > 0) {
                            mesg_buffer[n_read_write] = '\0';
                            syslog(LOG_INFO, "Parent received message from child %d: %s", active_children[i].pid, mesg_buffer);

                            // 받은 메시지를 다른 모든 활성 자식들에게 브로드캐스트합니다.
                            for (int j = 0; j < num_active_children; j++) {
                                // 메시지를 보낸 자식에게는 다시 보내지 않는 것이 일반적인 채팅 동작입니다.
                                // 귓속말 기능 구현 시 이 조건을 활용할 수 있습니다.
                                if (active_children[i].pid == active_children[j].pid) {
                                    continue; 
                                }

                                syslog(LOG_INFO, "Parent broadcasting to child %d.", active_children[j].pid);
                                
                                // 자식에게 메시지를 보내기 전, 해당 자식에게 SIGUSR1 시그널을 전송합니다.
                                // 이렇게 해야 자식의 블로킹 read()가 EINTR로 중단되어 부모의 메시지를 처리할 수 있습니다.
                                if (kill(active_children[j].pid, SIGUSR1) == -1) {
                                    syslog(LOG_ERR, "Parent: Failed to send SIGUSR1 to child %d: %m", active_children[j].pid);
                                } else {
                                    // 파이프에 메시지 쓰기 시도. O_NONBLOCK이 설정되어 있다면, 버퍼가 가득 차면 블로킹되지 않고 즉시 반환합니다.
                                    set_nonblocking(active_children[j].parent_to_child_write_fd);
                                    if (write(active_children[j].parent_to_child_write_fd, mesg_buffer, n_read_write) <= 0) {
                                        // EAGAIN/EWOULDBLOCK은 일시적인 현상이므로 오류로 처리하지 않을 수 있습니다.
                                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                             syslog(LOG_ERR, "Parent failed to broadcast to child %d: %m", active_children[j].pid);
                                             // 쓰기 실패 시 해당 자식과의 통신 문제로 간주하고 추가 처리 필요 (예: 연결 끊김)
                                        }
                                    }
                                    set_blocking(active_children[j].parent_to_child_write_fd); // 쓰기 후에는 다시 블로킹 모드로 복원합니다.
                                }
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
                continue; // 시그널 처리 후 다시 accept() 시도로 돌아갑니다.
            }
            syslog(LOG_ERR, "accept() error: %m"); // EINTR 외의 accept() 오류는 치명적입니다.
            exit(1); // 치명적 오류 발생 시 서버 종료
        }

        // 클라이언트의 IP 주소를 사람이 읽을 수 있는 문자열 형태로 변환하고 로깅합니다.
        inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_buffer, BUFSIZ);
        syslog(LOG_INFO, "Client is connected: %s", mesg_buffer);

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

        pid_t new_pid; 
        if((new_pid = fork()) < 0){ // fork() 호출: 새 프로세스를 생성합니다.
            syslog(LOG_ERR, "fork failed: %m");
            // fork 실패 시 열린 모든 리소스(소켓, 파이프)를 닫아 자원 누수를 방지합니다.
            close(csock);
            close(parent_pfd[0]); close(parent_pfd[1]);
            close(child_pfd[0]);  close(child_pfd[1]);
            continue; // 다음 클라이언트 연결을 시도합니다.
        }
        // --- 자식 프로세스 ---
        else if(new_pid == 0){ // 자식 프로세스는 fork()의 반환 값이 0입니다.
            syslog(LOG_INFO, "Child process started for PID %d.", getpid());
            setup_signal_handlers_child(); // 자식 프로세스에 시그널 핸들러를 설정합니다.

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
                    syslog(LOG_INFO, "Child: SIGUSR1 received, checking parent pipe for broadcast.");
                    
                    // 부모 파이프 FD를 논블로킹으로 설정하고 읽기 시도합니다.
                    // 데이터가 없으면 EAGAIN/EWOULDBLOCK을 반환하므로, 블로킹되지 않고 다음 코드로 진행합니다.
                    set_nonblocking(read_from_parent_pipe_fd);
                    child_n_read_write = read(read_from_parent_pipe_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);
                    set_blocking(read_from_parent_pipe_fd); // 읽기 후에는 다시 블로킹 모드로 복원합니다.
                    
                    if (child_n_read_write > 0) {
                        child_mesg_buffer[child_n_read_write] = '\0';
                        syslog(LOG_INFO, "Child received from parent (broadcast): %s", child_mesg_buffer);

                        // 부모로부터 받은 메시지를 해당 클라이언트에게 전달합니다.
                        // 클라이언트 소켓도 논블로킹으로 설정하고 쓰기 시도합니다.
                        set_nonblocking(client_socket_fd);
                        if (write(client_socket_fd, child_mesg_buffer, child_n_read_write) <= 0) {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                syslog(LOG_ERR, "Child failed to write to client (broadcast): %m");
                                break; // 쓰기 오류 시 통신 루프 종료
                            }
                        }
                        set_blocking(client_socket_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
                    } else if (child_n_read_write == 0) {
                        // 부모 파이프가 닫힘 (부모 프로세스가 종료되었거나 파이프의 모든 쓰기 끝이 닫힘)
                        syslog(LOG_INFO, "Child: Parent pipe closed. Exiting child loop.");
                        break; // 통신 루프 종료
                    } else { // child_n_read_write < 0
                        // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
                        // EINTR은 시그널에 의해 중단된 것이므로 실제 오류가 아닙니다.
                        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                            syslog(LOG_ERR, "Child read from parent pipe error: %m");
                            break; // 다른 오류 발생 시 통신 루프 종료
                        }
                    }
                }

                // 2. 클라이언트 소켓에서 메시지 읽기 시도 (블로킹)
                // 이 read()는 클라이언트가 메시지를 보낼 때까지 블로킹됩니다.
                // 부모가 SIGUSR1 시그널을 보내면 이 read()는 EINTR 오류로 중단될 수 있습니다.
                child_n_read_write = read(client_socket_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);

                if (child_n_read_write > 0) {
                    child_mesg_buffer[child_n_read_write] = '\0'; // 문자열 종료 처리
                    syslog(LOG_INFO, "Child received from client: %s", child_mesg_buffer);

                    // 클라이언트에게 받은 메시지를 부모에게 파이프를 통해 전달합니다.
                    // 부모에게 메시지를 보내기 전, 부모에게 SIGUSR1 시그널을 전송합니다.
                    // 이렇게 해야 부모의 accept()나 다른 read()가 EINTR로 중단되어 이 메시지를 처리할 수 있습니다.
                    if (kill(getppid(), SIGUSR1) == -1) { // getppid()로 부모 PID를 얻습니다.
                        syslog(LOG_ERR, "Child: Failed to send SIGUSR1 to parent: %m");
                    } 
                    // 파이프에 메시지 쓰기 시도 (이 write()도 파이프 버퍼가 가득 차면 블로킹될 수 있습니다.)
                    // 파이프 FD를 논블로킹으로 설정하고 쓰기 시도합니다.
                    set_nonblocking(write_to_parent_pipe_fd);
                    if (write(write_to_parent_pipe_fd, child_mesg_buffer, child_n_read_write) <= 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            syslog(LOG_ERR, "Child failed to write to parent pipe: %m");
                            break; // 쓰기 오류 시 통신 루프 종료
                        }
                    }
                    set_blocking(write_to_parent_pipe_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
                } else if (child_n_read_write == 0) {
                    // 클라이언트 연결 종료 (EOF): 클라이언트가 연결을 끊었습니다.
                    syslog(LOG_INFO, "Client disconnected. Exiting child loop.");
                    break; // 통신 루프 종료
                } else { // child_n_read_write < 0
                    // read 오류 발생. EINTR은 시그널에 의한 중단이므로 실제 오류가 아닙니다.
                    if (errno != EINTR) { 
                        syslog(LOG_ERR, "Child read from client error: %m");
                        break; // 다른 오류 발생 시 통신 루프 종료
                    }
                    // EINTR인 경우, 루프가 다시 시작되어 child_sigusr_arrived 플래그를 먼저 확인하게 됩니다.
                }
            } // --- while (1) 루프 종료 ---

            // 자식 프로세스 종료 전 모든 열린 파일 디스크립터를 닫습니다.
            // 자원 누수를 방지하고 운영체제에 FD를 반환합니다.
            close(client_socket_fd);
            close(read_from_parent_pipe_fd);
            close(write_to_parent_pipe_fd);
            syslog(LOG_INFO, "Child process exiting gracefully.");
            exit(0); // 자식 프로세스는 자신의 역할을 마치면 반드시 종료합니다.
        }
        // --- 부모 프로세스 ---
        else { // 부모 프로세스 (new_pid는 새로 생성된 자식의 PID를 가집니다.)
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
                active_children[num_active_children].pid = new_pid; 
                active_children[num_active_children].parent_to_child_write_fd = parent_pfd[1]; // 부모가 자식에게 쓸 FD
                active_children[num_active_children].child_to_parent_read_fd = child_pfd[0];   // 부모가 자식에게서 읽을 FD
                syslog(LOG_INFO, "Parent: Child %d added. Total active children: %d.", new_pid, num_active_children + 1);
                num_active_children++; // 활성화된 자식 수 증가
            } else {
                syslog(LOG_WARNING, "Parent: MAX_CLIENT limit reached. Not managing child %d.", new_pid);
                // 최대 클라이언트 수 초과 시, 이미 생성된 파이프 FD를 닫고 자식에게 종료 시그널을 보냅니다.
                // 이렇게 하면 자식 프로세스가 불필요하게 실행되지 않도록 합니다.
                close(parent_pfd[1]); 
                close(child_pfd[0]);
                kill(new_pid, SIGTERM); // 자식 프로세스를 종료하도록 요청합니다.
            }
            
            // waitpid()는 SIGCHLD 핸들러에서 비동기적으로 처리되어야 합니다.
            // 이곳에 두면 accept()가 블로킹되어 다른 클라이언트 연결을 받지 못합니다.
            // 따라서 main 루프에서는 waitpid()를 직접 호출하지 않습니다.
            // waitpid(new_pid, &status, 0); // 이 줄은 제거되었습니다.
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

// 메인 루프 안에서
if (client_message_arrived) {
    // 모든 클라이언트 서버의 파이프를 순회하며 메시지 읽기 시도
    for (int i = 0; i < client_num; i++) {
        if (client_pipe_info[i].isActive) {
            char mesg_buffer[BUFSIZ];
            ssize_t bytes_read;

            bytes_read = read(client_pipe_info[i].child_to_parent_read_fd, mesg_buffer, BUFSIZ - 1);

            if (bytes_read > 0) {
                mesg_buffer[bytes_read] = '\0';
                syslog(LOG_INFO, "Main: Received from client %d: %s", client_pipe_info[i].pid, mesg_buffer);

                // --- 여기서 메시지 파싱 및 처리 로직 시작 ---
                // 예시: "/add chatroom_name" 파싱
                if (mesg_buffer[0] == '/' && strncmp(mesg_buffer + 1, "add ", 4) == 0) {
                    char room_name[MAX_NAME_LEN];
                    // room_name 추출 로직 (예: sscanf 또는 strtok 사용)
                    // sscanf(mesg_buffer, "/add %s", room_name);

                    // 1. 새로운 채팅방 서버 프로세스 생성 (fork!)
                    int main_to_room_pipe[2]; // 메인 -> 채팅방
                    int room_to_main_pipe[2]; // 채팅방 -> 메인

                    if (pipe(main_to_room_pipe) == -1 || pipe(room_to_main_pipe) == -1) {
                        syslog(LOG_ERR, "Pipe creation failed for new chat room: %m");
                        // 클라이언트에게 에러 메시지 전송
                        continue;
                    }

                    pid_t room_pid = fork();
                    if (room_pid == -1) {
                        syslog(LOG_ERR, "Fork failed for new chat room: %m");
                        // 클라이언트에게 에러 메시지 전송
                        close(main_to_room_pipe[0]); close(main_to_room_pipe[1]);
                        close(room_to_main_pipe[0]); close(room_to_main_pipe[1]);
                        continue;
                    } else if (room_pid == 0) { // 자식: 새로운 채팅방 서버 프로세스
                        close(main_to_room_pipe[1]); // 자식은 부모로부터 읽기
                        close(room_to_main_pipe[0]); // 자식은 부모에게 쓰기
                        // 메인 서버의 모든 소켓, 파이프 FD 닫기 (자식에게 불필요)
                        // ...

                        // 채팅방 서버 로직 실행
                        run_chatroom_server_logic(room_name, main_to_room_pipe[0], room_to_main_pipe[1]);
                        exit(0); // 채팅방 서버는 여기서 종료
                    } else { // 부모: 메인 서버 프로세스
                        close(main_to_room_pipe[0]); // 부모는 자식에게 쓰기
                        close(room_to_main_pipe[1]); // 부모는 자식으로부터 읽기

                        // 채팅방 서버 정보 저장
                        chat_room_info[room_num].pid = room_pid;
                        chat_room_info[room_num].parent_to_child_write_fd = main_to_room_pipe[1];
                        chat_room_info[room_num].child_to_parent_read_fd = room_to_main_pipe[0];
                        chat_room_info[room_num].isActive = true;
                        strcpy(chat_room_info[room_num].name, room_name); // 방 이름 저장
                        room_num++;

                        syslog(LOG_INFO, "Main: Chat room '%s' (PID: %d) created.", room_name, room_pid);
                        // 클라이언트에게 방 생성 성공 메시지 전송
                        // (client_pipe_info[i].parent_to_child_write_fd 사용)
                    }
                } else {
                    // 일반 채팅 메시지 또는 다른 명령어 처리
                    // ...
                }
                // --- 메시지 파싱 및 처리 로직 끝 ---
            }
            // ... (bytes_read == 0, bytes_read == -1 처리)
        }
    }
    client_message_arrived = 0; // 모든 클라이언트 서버의 파이프를 확인했으므로 플래그 초기화
}

