#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h> // select()를 위해 필요
#include <sys/types.h> // select()를 위해 필요

#define TCP_PORT 5100
#define TIMEOUT_SEC 5 // select()의 타임아웃 시간 (초)

int main(int argc, char **argv)
{
    int ssock; // 클라이언트 소켓
    struct sockaddr_in servaddr;
    char send_mesg[BUFSIZ]; // 보낼 메시지 버퍼
    char recv_mesg[BUFSIZ]; // 받을 메시지 버퍼
    
    // select() 관련 변수
    fd_set read_fds;    // 읽기 가능한 파일 디스크립터 집합
    int max_fd;         // select()에 전달할 최대 파일 디스크립터 번호 + 1

    if (argc < 2) {
        fprintf(stderr, "Usage : %s IP_ADDRESS\n", argv[0]);
        return -1;
    }

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;

    // IP 주소 변환 및 설정 (inet_pton의 세 번째 인자는 in_addr 구조체 포인터)
    if (inet_pton(AF_INET, argv[1], &(servaddr.sin_addr)) <= 0) {
        perror("inet_pton() - Invalid IP address");
        close(ssock);
        return -1;
    }
    servaddr.sin_port = htons(TCP_PORT);

    // 서버에 연결
    if (connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        close(ssock);
        return -1;
    }

    printf("Connected to server %s:%d. Type a message to send (type 'quit' to exit):\n", argv[1], TCP_PORT);

    // 메인 루프: 표준 입력과 소켓을 동시에 모니터링
    while (1) {
        FD_ZERO(&read_fds);         // 파일 디스크립터 집합 초기화
        FD_SET(STDIN_FILENO, &read_fds); // 표준 입력 (fd 0)을 읽기 집합에 추가
        FD_SET(ssock, &read_fds);   // 소켓을 읽기 집합에 추가

        // max_fd는 모니터링할 파일 디스크립터 중 가장 큰 값 + 1
        max_fd = (STDIN_FILENO > ssock) ? STDIN_FILENO : ssock;
        max_fd++;

        // 타임아웃 설정 (선택 사항: NULL이면 무한 대기)
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT_SEC;
        timeout.tv_usec = 0;

        printf("Waiting for input or server response...\n");

        // select() 호출: 읽기 가능하거나, 쓰기 가능하거나, 예외 상황이 발생한 fd를 기다림
        // 현재는 읽기만 모니터링
        int activity = select(max_fd, &read_fds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("select()");
            break; // select 에러 발생 시 루프 종료
        } else if (activity == 0) {
            // 타임아웃 발생 (TIMEOUT_SEC 동안 아무 활동 없음)
            printf("Timeout occurred. No activity for %d seconds.\n", TIMEOUT_SEC);
            continue; // 다시 대기
        }

        // 1. 표준 입력으로부터 읽을 데이터가 있는지 확인
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(send_mesg, 0, BUFSIZ); // 버퍼 초기화
            if (fgets(send_mesg, BUFSIZ, stdin) == NULL) {
                // fgets 에러 또는 EOF (Ctrl+D)
                printf("End of input from stdin. Exiting.\n");
                break;
            }

            // 'quit' 입력 시 클라이언트 종료
            if (strncmp(send_mesg, "quit\n", 5) == 0) {
                printf("Client quitting.\n");
                break;
            }

            // 서버로 메시지 전송
            // MSG_DONTWAIT 플래그는 여기서 필요 없습니다. select()가 이미 논블로킹 처리를 하고 있습니다.
            if (send(ssock, send_mesg, strlen(send_mesg), 0) <= 0) {
                perror("send()");
                break; // send 에러 발생 시 루프 종료
            }
            printf("Sent: %s", send_mesg);
        }

        // 2. 소켓으로부터 읽을 데이터 (서버 응답)가 있는지 확인
        if (FD_ISSET(ssock, &read_fds)) {
            memset(recv_mesg, 0, BUFSIZ); // 버퍼 초기화
            int bytes_received = recv(ssock, recv_mesg, BUFSIZ - 1, 0); // 널 종료를 위해 -1

            if (bytes_received <= 0) {
                if (bytes_received == 0) {
                    // 서버가 연결을 정상적으로 종료했거나 더 이상 보낼 데이터가 없음 (EOF)
                    printf("Server closed connection.\n");
                } else {
                    // recv 에러 발생
                    perror("recv()");
                }
                break; // 서버 연결 끊김 또는 에러 발생 시 루프 종료
            }
            
            recv_mesg[bytes_received] = '\0'; // 널 종료 문자 추가
            printf("Received data from server: %s\n", recv_mesg);
        }
    }

    // 소켓 닫기
    close(ssock);
    printf("Socket closed. Client exiting.\n");

    return 0;
}