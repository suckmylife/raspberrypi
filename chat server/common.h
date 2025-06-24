#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <unistd.h>

#define TCP_PORT   5100
#define MAX_CLIENT 32
#define CHAT_ROOM  4
#define NAME       50

// 각 자식 프로세스의 파이프 정보를 담는 구조체
// fd가 부모관점인 이유는 자식과 통신하기 위해 그런 것
typedef struct {
    // 자식 프로세스의 PID
    pid_t pid;             
    // 부모가 이 자식에게 메시지를 보낼 때 사용하는 파이프의 '쓰기' 끝 FD (부모 관점)
    int parent_to_child_write_fd;
    // 이 자식이 부모에게 메시지를 보낼 때, 부모가 '읽을' 파이프의 FD (부모 관점)
    int child_to_parent_read_fd;
    //활성 비활성 트리거
    bool isActive;
    char name[NAME]; // 닉네임
    char room_name[NAME]; // 채팅방 이름
} pipeInfo;

typedef struct {
    char name[NAME];   // 채팅방 이름
} roomInfo;

extern struct roomInfo room_info[CHAT_ROOM]; // 채팅방 목록
extern struct pipeInfo client_pipe_info[CHAT_ROOM]; // 클라이언트서버 & 메인 서버의 파이프 

// volatile을 사용하는 이유
/*
    레지스터 캐싱
    컴파일러는 코드를 분석해서 "이 변수는 루프 안에서 계속 
    같은 값을 읽네? 그럼 매번 메모리에서 읽지 말고, 
    레지스터 같은 빠른 곳에 저장해두고 그걸 계속 쓰자!" 
    라고 판단

    1. 메인 루프가 flag 변수를 읽어서 레지스터에 캐싱
    2. 메인 루프는 캐싱된 flag 값을 계속 사용하면서 
       if (flag) 조건을 확인
    3. 이때 시그널이 발생해서 시그널 핸들러가 실행
    4. 시그널 핸들러는 flag = 1;이라고 메모리에 있는 
       flag 값을 실제로 바꿈
    5. 하지만 메인 루프는 여전히 캐싱된 오래된 flag 값(0)을 
       사용 메인 루프는 메모리에 있는 flag 값이 바뀌었는지 모름
       캐싱된 값만 계속 확인
    6. 결과적으로, 시그널 핸들러가 flag 값을 바꿨음에도 불구하고, 
       메인 루프는 그 변화를 감지하지 못하고 무한 루프에 빠지거나 
       잘못된 동작을 함
    
    이 문제를 막기 위해 컴파일러 최적화를 막기 위해 volatile 사용
*/

// sig_atomic_t을 사용하는 이유 
/*
    CPU는 보통 1바이트 데이터의 원자적 연산이 보장되나 
    4바이트같은 int 변수는 CPU에서 여러번 걸쳐 연산
    이 때문에 값이 쓰레기 값이 됨

    이를 방지하기위해 한번에 완료되도록 보장하기 위해
    sig_atomic_t을 씀
*/
extern volatile sig_atomic_t is_write_from_chat_room; // 채팅서버가 쓴다
extern volatile sig_atomic_t is_write_from_client;    // 클라이언트 서버가 쓴다
extern volatile sig_atomic_t is_shutdown;             // 종료 신호
extern volatile sig_atomic_t child_exited_flag;       // 좀비방지하라는 신호
extern volatile sig_atomic_t client_num;              // 활성화된 클라이언트 수
extern volatile sig_atomic_t room_num;                // 활성화된 채팅방 수

#endif // COMMON_H