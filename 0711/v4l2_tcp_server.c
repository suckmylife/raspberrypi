#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <errno.h>  // EAGAIN, EWOULDBLOCK 오류 처리를 위한 헤더
#include <sys/time.h>  // select() 함수 사용을 위한 헤더
#include <sys/select.h>
#define TCP_PORT 5100
#define WIDTH 640
#define HEIGHT 480
#define FRAMEBUFFER_DEVICE "/dev/fb0"

static struct fb_var_screeninfo vinfo;

void display_frame(uint16_t *fbp, uint8_t *data, int width, int height) 
{
  int x_offset = (vinfo.xres - width) / 2;
  int y_offset = (vinfo.yres - height) / 2;

  // YUYV -> RGB565 변환하여 프레임버퍼에 출력
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; x += 2) {
      uint8_t Y1 = data[(y * width + x) * 2];
      uint8_t U = data[(y * width + x) * 2 + 1];
      uint8_t Y2 = data[(y * width + x + 1) * 2];
      uint8_t V = data[(y * width + x + 1) * 2 + 1];

      int R1 = Y1 + 1.402 * (V - 128);
      int G1 = Y1 - 0.344136 * (U - 128) - 0.714136 * (V - 128);
      int B1 = Y1 + 1.772 * (U - 128);

      int R2 = Y2 + 1.402 * (V - 128);
      int G2 = Y2 - 0.344136 * (U - 128) - 0.714136 * (V - 128);
      int B2 = Y2 + 1.772 * (U - 128);

      // RGB565 포맷으로 변환 (R: 5비트, G: 6비트, B: 5비트)
      uint16_t pixel1 = ((R1 & 0xF8) << 8) | ((G1 & 0xFC) << 3) | (B1 >> 3);
      uint16_t pixel2 = ((R2 & 0xF8) << 8) | ((G2 & 0xFC) << 3) | (B2 >> 3);

      fbp[(y + y_offset) * vinfo.xres + (x + x_offset)] = pixel1;
      fbp[(y + y_offset) * vinfo.xres + (x + x_offset + 1)] = pixel2;
    }
  }
}

int main(int argc, char **argv)
{
    int ssock;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    char mesg[BUFSIZ];
    
    // 프레임버퍼 초기화
    int fb_fd = open(FRAMEBUFFER_DEVICE, O_RDWR);
    if (fb_fd == -1) {
        perror("Error opening framebuffer device");
        exit(1);
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information");
        close(fb_fd);
        exit(1);
    }

    uint32_t fb_width = vinfo.xres;
    uint32_t fb_height = vinfo.yres;
    uint32_t screensize = fb_width * fb_height * vinfo.bits_per_pixel / 8;
    uint16_t *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if ((intptr_t)fbp == -1) {
        perror("Error mapping framebuffer device to memory");
        close(fb_fd);
        exit(1);
    }

    if((ssock = socket(AF_INET,SOCK_STREAM, 0))<0){
        perror("socket()");
        return -1;
    }

    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);

    if(bind(ssock,(struct sockaddr *)&servaddr,sizeof(servaddr))<0){
        perror("bind()");
        return -1;
    }

    if(listen(ssock,8)<0){
        perror("listen()");
        return -1;
    }

    clen = sizeof(cliaddr);
    int csock;
    while(1)
    {
        csock = accept(ssock,(struct sockaddr *)&cliaddr,&clen);
        if (csock < 0) {
            perror("accept");
            continue;
        }
        int buffer_size = 1024 * 1024; // 1MB
        setsockopt(csock, SOL_SOCKET, SO_RCVBUF, (char *)&buffer_size, sizeof(buffer_size));
        setsockopt(csock, SOL_SOCKET, SO_SNDBUF, (char *)&buffer_size, sizeof(buffer_size));
        // 서버 코드에서
        int flags = fcntl(csock, F_GETFL, 0);
        fcntl(csock, F_SETFL, flags | O_NONBLOCK);
        inet_ntop(AF_INET, &cliaddr.sin_addr,mesg,BUFSIZ);
        printf("Client is connected : %s\n",mesg);

        while (1) {
            int totalsize = 0;
            fd_set readfds;
            struct timeval tv;
            tv.tv_sec = 5;  // 5초 타임아웃
            tv.tv_usec = 0;
            
            FD_ZERO(&readfds);
            FD_SET(csock, &readfds);
            
            // select()를 사용하여 데이터가 도착할 때까지 효율적으로 대기
            int activity = select(csock + 1, &readfds, NULL, NULL, &tv);
            printf("after acitivity 136");
            if (activity < 0) {
                perror("select()");
                break;
            } else if (activity == 0) {
                // 타임아웃 발생
                printf("Timeout waiting for client data\n");
                continue;
            }
            printf("confirm activity 145");
            // 이제 데이터가 있으므로 recv() 호출
            int recv_result;
            while ((recv_result = recv(csock, &totalsize, sizeof(totalsize), 0)) < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 데이터가 아직 없음, 잠시 대기
                    usleep(1000);  // 1ms 대기
                    continue;
                } else {
                    // 실제 오류 발생
                    perror("recv() totalsize");
                    goto end_client_session;
                }
            }
            
            if (recv_result == 0) {
                // 클라이언트가 연결을 종료함
                printf("Client disconnected\n");
                break;
            }
            
            printf("Expected to receive: %d bytes\n", totalsize);
            
            // 데이터 수신을 위한 버퍼 할당
            char *buffer = (char*)malloc(totalsize);
            if (!buffer) {
                perror("malloc() failed");
                break;
            }
            
            // 프레임 데이터 수신
            int received = 0;
            while (received < totalsize) {
                
                // select()를 사용하여 데이터가 도착할 때까지 효율적으로 대기
                FD_ZERO(&readfds);
                FD_SET(csock, &readfds);
                tv.tv_sec = 2;  // 2초 타임아웃
                tv.tv_usec = 0;
                
                activity = select(csock + 1, &readfds, NULL, NULL, &tv);
                
                if (activity <= 0) {
                    if (activity < 0) {
                        perror("select() during data reception");
                    } else {
                        printf("Timeout waiting for frame data\n");
                    }
                    break;
                }
                
                // 적절한 청크 크기로 수신
                int to_receive = (totalsize - received > 4000) ? 4000 : (totalsize - received);
                
                int bytes = recv(csock, buffer + received, to_receive, 0);
                if (bytes < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 데이터가 아직 없음, 잠시 대기
                        usleep(1000);  // 1ms 대기
                        continue;
                    } else {
                        // 실제 오류 발생
                        perror("recv() buffer data");
                        free(buffer);
                        goto end_client_session;
                    }
                } else if (bytes == 0) {
                    // 클라이언트가 연결을 종료함
                    printf("Client disconnected during data transfer\n");
                    free(buffer);
                    goto end_client_session;
                }
                
                received += bytes;
            }
            
            // 모든 데이터를 성공적으로 받았는지 확인
            if (received == totalsize) {
                // 실제 프레임버퍼에 직접 출력
                display_frame(fbp, (uint8_t *)buffer, WIDTH, HEIGHT);
                printf("Frame displayed on framebuffer successfully.\n");
                
                // 클라이언트에게 최종 완료 응답 보내기 (성공: 1)
                int final_ack = 1;
                if (send(csock, &final_ack, sizeof(final_ack), 0) <= 0) {
                    perror("send() final ack");
                    free(buffer);
                    goto end_client_session;
                }
            } else {
                printf("Failed to receive complete frame data (%d/%d bytes)\n", received, totalsize);
                
                // 클라이언트에게 최종 완료 응답 보내기 (실패: 0)
                int final_ack = 0;
                if (send(csock, &final_ack, sizeof(final_ack), 0) <= 0) {
                    perror("send() final ack (error)");
                    free(buffer);
                    goto end_client_session;
                }
            }
            
            // 할당된 메모리 해제
            free(buffer);
            printf("Memory freed for this frame.\n");
            
            // 다음 프레임을 기다리기 위해 내부 루프의 처음으로 돌아감
        }
    }
end_client_session:
    // 클라이언트 소켓 닫기
    close(csock);
    printf("Client disconnected.\n");
    // 다음 클라이언트 연결을 기다리기 위해 외부 루프의 처음으로 돌아감
}