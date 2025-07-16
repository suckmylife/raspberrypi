#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>  // EAGAIN, EWOULDBLOCK 오류 처리를 위한 헤더
#include <sys/time.h>  // select() 함수 사용을 위한 헤더

#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/videodev2.h>

#define TCP_PORT 5100

/* 비디오 관련 정의*/
#define VIDEO_DEVICE        "/dev/video0" //카메라 접근
#define WIDTH               640           //해상도 선정
#define HEIGHT              480
/* 비디오 */

// 데이터 타입 정의 (서버와 동일하게 0으로 정의)
#define VIDEO_TYPE 0
// send()를 반복 호출하여 정확히 len 바이트를 모두 보내는 함수
int send_all(int sock, const void *buffer, size_t len) {
    size_t total_sent = 0;
    const char *buf = (const char *)buffer;

    while (total_sent < len) {
        ssize_t sent = send(sock, buf + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // 1ms 대기 후 재시도
                continue;
            } else {
                perror("send() error");
                return -1;
            }
        }
        total_sent += sent;
    }
    return 0;
}
int main(int argc, char **argv) {
    int ssock;
    struct sockaddr_in servaddr;
    
    // 서버 소켓 생성
    if((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    
    // 서버 주소 설정
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  // 서버 IP 주소
    servaddr.sin_port = htons(TCP_PORT);
    
    // 서버에 연결
    if(connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }
    
    // 소켓 버퍼 크기 증가(혹시나 소켓 버퍼 크기가 작아서 과부하 올까봐 추가)
    int buffer_size = 1024 * 1024;  // 1MB
    setsockopt(ssock, SOL_SOCKET, SO_RCVBUF, (char *)&buffer_size, sizeof(buffer_size));
    setsockopt(ssock, SOL_SOCKET, SO_SNDBUF, (char *)&buffer_size, sizeof(buffer_size));
    
    // 논블로킹 모드로 설정 
    /*
        클라이언트에서는 while문으로 와다다 프레임 버퍼를
        넘겨주고 있는데 서버에서 오는 데이터도 같이 
        읽어야 하니까 논블록 해줌.
        
        서버는 클라이언트에서 온 데이터를 다 읽어야 지만 
        응답을 하는데 클라이언트에서 서버에서 응답이 올때까지
        기다리고 있으면 프로그램이 멈춰있게 된다.
        이것을 방지하기 위해서 논블록킹을 해주는 것
    */
    int flags = fcntl(ssock, F_GETFL, 0);
    fcntl(ssock, F_SETFL, flags | O_NONBLOCK);
    
    // 프레임 전송 루프
     ////////////////////////////////////////////////////
     //카메라에 접근한다는 뜻
    int fd = open(VIDEO_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("Failed to open video device");
        return 1;
    }
    //카메라 캡쳐 1프레임 데이터 가져오는 세팅 부분
    //1프레임을 담기 위한 그릇 준비
    struct v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Failed to set format");
        close(fd);
        return 1;
    }

    char *buffer = malloc(fmt.fmt.pix.sizeimage);
    if (!buffer) {
        perror("Failed to allocate buffer");
        close(fd);
        return 1;
    }
    // 그릇 준비 끗!

    while (1) {
        //담았다! 1프레임!
        int totalsize = read(fd, buffer, fmt.fmt.pix.sizeimage);  // 클라이언트에서 프레임 읽기
        if (totalsize <= 0) {
            perror("Failed to read frame");
            break;
        }
        printf("totalsize : %d\n", totalsize);
        
        // 1. 데이터 타입 전송 (1바이트)
        if (send_all(ssock, &data_type, sizeof(data_type)) < 0) {
            fprintf(stderr, "Failed to send data type\n");
            break;
        }

        // 2. 데이터 크기 전송 (4바이트)
        if (send_all(ssock, &totalsize, sizeof(totalsize)) < 0) {
            fprintf(stderr, "Failed to send totalsize\n");
            break;
        }

        // 3. 실제 데이터 전송
        if (send_all(ssock, buffer, totalsize) < 0) {
            fprintf(stderr, "Failed to send frame data\n");
            break;
        }
        
        
        // 3. 서버로부터 최종 완료 응답을 기다림
        int final_server_response;
        fd_set readfds;
        struct timeval tv;
        
        // select()를 사용하여 데이터가 도착할 때까지 효율적으로 대기
        FD_ZERO(&readfds);
        FD_SET(ssock, &readfds);
        tv.tv_sec = 5;  // 5초 타임아웃
        tv.tv_usec = 0;
        // 서버가 나 다 받았어요 할때까지 기다림.(select 이용)
        // select (알아서 논블록킹 블록해준다 딸깍)
        int activity = select(ssock + 1, &readfds, NULL, NULL, &tv);
        
        //너무 오랫동안 서버에서 응답안하면 에러나게 됨
        if (activity < 0) { 
            perror("select()");
            break;
        } else if (activity == 0) {
            // 타임아웃 발생
            printf("Timeout waiting for server response\n");
            break;
        }
        
        // 데이터가 도착했으므로 recv() 호출
        int recv_result;
        //activity에서(즉 select) 서버에서 응답이 왔다고 알려줬으니까
        // 서버에서 온 "나 너가 보내준 데이터 다읽엇어"라는 응답을 읽기 시작
        // 이때 응답은 내가 서버에서 설정한 "1" : 성공 /"0" : 실패이다. 
        if (recv_all(ssock, &final_server_response, sizeof(final_server_response)) < 0) {
            perror("recv_all() final server response");
            break; // 오류 발생 시 종료
        }

        
        // 서버가 모든 데이터를 성공적으로 받았음을 확인
        if (final_server_response == 1) {
            printf("Frame sent successfully and confirmed by server.\n");
        } else {
            //실패함!
            fprintf(stderr, "Server did not confirm successful reception.\n");
            break;
        }
    }
    
cleanup:
    close(ssock);
    return 0;
}