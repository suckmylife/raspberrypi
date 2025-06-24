#ifndef SIGNAL_H
#define SIGNAL_H

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "common.h"

#define SIG_FROM_CHATROOM SIGUSR1  // 채팅방에서 메시지가 왔을 때
#define SIG_FROM_CLIENT SIGUSR2    // 클라이언트에서 메시지가 왔을 때
// 채팅방 서버에서 메시지를 보냈다는 신호 SIGUSR1 사용
void from_chatroom_mesg_handler(int signum); 
// 클라이언트 서버에서 메시지를 보냈다는 신호 SIGUSR2 사용
void from_client_mesg_handler(int signum); 

// 채팅방 & 클라이언트 서버 프로세스 종료 설정
void child_close_handler(); 
// child_close_handler에 들어갈 좀비방지 코드
void handler_sigchld(int signum); // 메인서버의 자식들 좀비 방지

//메인서버 종료 신호(SIGINT, SIGTERM)
void grace_close_handler(int signum); //종료신호 방출

//사용자 정의 신호 설정
void setup_chatroom_handler(); //채팅방 신호 핸들러 설정
void setup_client_handler();   //클라이언트서버 신호 핸들러 설정

//활성화된 프로세스 정리
void clean_active_process();

int set_nonblocking(int fd);


#endif // SIGNAL_H