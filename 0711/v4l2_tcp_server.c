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
    while(1)
    {
        int csock = accept(ssock,(struct sockaddr *)&cliaddr,&clen);
        if (csock < 0) {
            perror("accept");
            continue;
        }
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
        char *buffer = (char*)malloc(totalsize);  // 또는 충분한 크기로 할당된 배열
        
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
        uint16_t *processed_frame_output_buffer = (uint16_t *)malloc(WIDTH * HEIGHT * sizeof(uint16_t));
        if (processed_frame_output_buffer == NULL) {
            perror("malloc() processed_frame_output_buffer");
            free(buffer);
            close(csock);
            return -1;
        }
        printf("Buffer allocated for processed frame output: %d bytes\n", WIDTH * HEIGHT * sizeof(uint16_t));

        // 6. display_frame 함수 호출
        // received_frame_data_buffer를 uint8_t*로 캐스팅하여 'data' 인자로 전달합니다.
        // processed_frame_output_buffer를 'fbp' 인자로 전달합니다.
        display_frame(processed_frame_output_buffer, (uint8_t *)buffer, WIDTH, HEIGHT);
        printf("display_frame function called successfully. Processed data is in 'processed_frame_output_buffer'.\n");

        // 7. 처리된 프레임 데이터 활용 (서버의 다음 로직)
        // 'processed_frame_output_buffer'에는 이제 display_frame 함수를 통해 변환된
        // uint16_t 형식의 픽셀 데이터가 들어 있습니다. 이 데이터를 어떻게 활용할지는
        // 서버의 목적에 따라 달라집니다.
        // 예시:
        // - 이 데이터를 다시 압축하여 다른 클라이언트에게 스트리밍
        // - 이 데이터를 파일로 저장
        // - 서버 내부에서 이미지 처리 또는 분석에 활용

        // 8. 할당된 메모리 해제
        free(buffer);
        free(processed_frame_output_buffer);
        printf("Memory freed.\n");

        // 클라이언트 소켓 닫기
        close(csock);
        printf("Client disconnected.\n");
    }
    
    
   
    return 0;
}