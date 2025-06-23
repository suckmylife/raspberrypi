#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <unistd.h>

#define TCP_PORT   5100
#define MAX_CLIENT 32
#define CHAT_ROOM  4
#define NAME       50

// 각 자식 프로세스의 파이프 정보를 담는 구조체
typedef struct {
    // 자식 프로세스의 PID
    pid_t pid;             
    // 부모가 이 자식에게 메시지를 보낼 때 사용하는 파이프의 '쓰기' 끝 FD (부모 관점)
    int parent_to_child_write_fd;
    // 이 자식이 부모에게 메시지를 보낼 때, 부모가 '읽을' 파이프의 FD (부모 관점)
    int child_to_parent_read_fd;
} pipeInfo;

typedef struct {
    pid_t pid;         // 채팅방 서버의 PID
    char name[NAME];   // 채팅방 이름
    int user_count;    // 현재 채팅방에 있는 사용자 수
} roomInfo;

struct roomInfo room_info[CHAT_ROOM]; // 채팅방 목록

struct pipeInfo chat_pipe_info[MAX_CLIENT]; // 채팅서버 & 클라이언트 서버의 파이프 
struct pipeInfo main_pipe_info[CHAT_ROOM]; // 채팅서버 & 메인 서버의 파이프 

#endif // COMMON_H