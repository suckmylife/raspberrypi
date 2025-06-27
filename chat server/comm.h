#ifndef COMM_H
#define COMM_H

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
extern roomInfo room_info[CHAT_ROOM];  
extern pipeInfo active_children[MAX_CLIENT]; 
extern volatile int num_active_children; //활성화된 자식 프로세스(클라이언트 수);
extern volatile int room_num;  // 만들어진 채팅방의 수

extern volatile sig_atomic_t parent_sigusr_arrived;  //부모에서 쓴다
extern volatile sig_atomic_t child_sigusr_arrived;   //자식에서 쓴다
extern volatile sig_atomic_t child_exited_flag;      //자식 죽음(클라이언트 종료)

// --- FCNTL 관련 함수 ---
int set_nonblocking(int fd);
int set_blocking(int fd);
// 명령어 검사 함수
int check_command(const char* mesg, const char* command);
#endif // COMM_H