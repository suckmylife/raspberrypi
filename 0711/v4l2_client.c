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

/* 비디오  관련 정의*/
#define VIDEO_DEVICE        "/dev/video0" //카메라 접근
#define WIDTH               640           //해상도 선정
#define HEIGHT              480
/* 비디오 */


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
        
        // 1. 총 사이즈 보내기
        // : 뜻 --> 서버 선생님 제가 이만큼 보낼거니까 준비하셔요
        int send_result;
        while ((send_result = send(ssock, &totalsize, sizeof(totalsize), 0)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 송신 버퍼가 가득 찼을 때 잠시 대기
                usleep(1000);  // 1ms 대기
                continue;
            } else {
                // 실제 오류 발생
                perror("send() totalsize");
                goto cleanup;
            }
        }
        
        // 2. 버퍼 전송 (위에서 논블록킹 해준 효과 : 서버 응답을 기다리지 않고 연속적으로 전송)
        // : 뜻 --> 지금부터 데이터 진짜 보내요 갑니다~
        // 버퍼가 왜 쪼개져서 갈까? :TCP가 알아서 쪼개서 보내준다고 합니다
        int sent = 0;
        //자 이제 다보낼때까지(sent가 totalsize될때까지) 와다다 쪼개서 보냅니다~
        while (sent < totalsize) {
            //이 청크사이즈는
            //만약 쪼개서 보낼때 8kb보다 커지면 부하오니까 8kb넘지않게 보내라는 뜻
            int chunk_size = (totalsize - sent > 8192) ? 8192 : (totalsize - sent);
            
            int bytes_sent;
            while ((bytes_sent = send(ssock, buffer + sent, chunk_size, 0)) < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 송신 버퍼가 가득 찼을 때 잠시 대기
                    // 서버에서 못받고 허덕이고 있으면 대기해라
                    usleep(1000);  // 1ms 대기
                    continue;
                } else {
                    // 실제 오류 발생
                    perror("send() buffer chunk");
                    goto cleanup;
                }
            }
            
            sent += bytes_sent;
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
        while ((recv_result = recv(ssock, &final_server_response, sizeof(final_server_response), 0)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 데이터가 아직 없음, 잠시 대기
                usleep(1000);  // 1ms 대기
                continue;
            } else {
                // 실제 오류 발생
                perror("recv() final server response");
                goto cleanup;
            }
        }
        
        if (recv_result == 0) {
            // 연결이 종료됨
            printf("Server closed connection\n");
            break;
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