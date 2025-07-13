#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#define TCP_PORT 5100
#define VIDEO_DEVICE        "/dev/video0"
#define FRAMEBUFFER_DEVICE  "/dev/fb0"
#define WIDTH               640
#define HEIGHT              480

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
    do{
        int n, csock = accept(ssock,(struct sockaddr *)&cliaddr,&clen);

        inet_ntop(AF_INET, &cliaddr.sin_addr,mesg,BUFSIZ);
        printf("Client is connected : %s\n",mesg);
        // 서버 측 코드 예시
        int totalsize;
        if (recv(csock, &totalsize, sizeof(totalsize), 0) <= 0) {
            perror("recv() totalsize");
            return -1;
        }

        // 클라이언트에게 응답 보내기
        int ack = 1;
        if (send(csock, &ack, sizeof(ack), 0) <= 0) {
            perror("send() acknowledgment");
            return -1;
        }
        
        // 데이터 수신
        int received = 0;
        char *buffer = malloc(totalsize);  // 또는 충분한 크기로 할당된 배열
        while (received < totalsize) {
            int bytes = recv(csock, buffer + received, totalsize - received, 0);
            if (bytes <= 0) {
                perror("recv() buffer data");
                free(buffer);
                return -1;
            }
            
            received += bytes;
            
            // 현재까지 읽은 위치를 클라이언트에게 알림
            if (send(csock, &received, sizeof(received), 0) <= 0) {
                perror("send() read position");
                return -1;
            }
        }
        uint32_t fb_width = vinfo.xres;
        uint32_t fb_height = vinfo.yres;
        uint32_t screensize = fb_width * fb_height * vinfo.bits_per_pixel / 8;
        uint16_t *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE,                                         MAP_SHARED, fb_fd, 0);
        
        if ((intptr_t)fbp == -1) {
            perror("Error mapping framebuffer device to memory");
            close(fb_fd);
            exit(1);
        }

        display_frame(fbp, buffer, WIDTH, HEIGHT);
        
    }while(strncmp(mesg,"q",1));
    free(buffer);
    close(ssock);
    return 0;
}