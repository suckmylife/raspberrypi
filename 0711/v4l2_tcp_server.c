#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h> // 비디오 관련 헤더 복구
#include <sys/mman.h>  // 비디오 관련 헤더 복구
#include <linux/fb.h>  // 비디오 관련 헤더 복구
#include <linux/videodev2.h> // 비디오 관련 헤더 복구
#include <errno.h>     // EAGAIN, EWOULDBLOCK 오류 처리를 위한 헤더
#include <sys/time.h>  // select() 함수 사용을 위한 헤더
#include <sys/select.h>

// PulseAudio 오디오 재생을 위한 헤더 추가
#include <pulse/simple.h>
#include <pulse/error.h>

#define TCP_PORT 5100
#define WIDTH 640  // 비디오 관련 정의 복구
#define HEIGHT 480 // 비디오 관련 정의 복구

#define FRAMEBUFFER_DEVICE "/dev/fb0" // 비디오 관련 정의 복구

// 데이터 타입 정의
#define VIDEO_TYPE 0
#define AUDIO_TYPE 1

static struct fb_var_screeninfo vinfo; // 비디오 관련 변수 복구
static uint16_t *fbp = NULL; // 프레임버퍼 포인터 전역 변수로 선언

// PulseAudio 출력 스트림 전역 변수
static pa_simple *audio_output = NULL;

// 교수님 코드 복사>< (비디오 디스플레이 함수 복구)
void display_frame(uint16_t *fbp_ptr, uint8_t *data, int width, int height)
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

      fbp_ptr[(y + y_offset) * vinfo.xres + (x + x_offset)] = pixel1;
      fbp_ptr[(y + y_offset) * vinfo.xres + (x + x_offset + 1)] = pixel2;
    }
  }
}

// 정확히 len 바이트를 모두 받을 때까지 반복하는 함수
int recv_all(int sock, void *buffer, size_t len) {
    size_t total = 0;
    char *buf = (char *)buffer;
    while (total < len) {
        ssize_t n = recv(sock, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 데이터가 아직 도착하지 않음, 잠시 대기
                usleep(1000);
                continue;
            } else {
                perror("recv() error");
                return -1;
            }
        } else if (n == 0) {
            // 연결 종료
            printf("Client disconnected during recv_all\n");
            return -1;
        }
        total += n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int ssock;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    char mesg[BUFSIZ];
    int error; // PulseAudio 에러 처리를 위한 변수

    // 1. 프레임버퍼 초기화 (비디오 관련 코드 복구)
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
    fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0); // 전역 fbp에 할당
    if ((intptr_t)fbp == -1) {
        perror("Error mapping framebuffer device to memory");
        close(fb_fd);
        exit(1);
    }
    printf("Framebuffer initialized.\n");

    // 2. PulseAudio 샘플 사양 설정 및 출력 스트림 생성
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = 44100; // audio.txt의 SAMPLE_RATE와 동일
    ss.channels = 2; // audio.txt의 CHANNELS와 동일

    audio_output = pa_simple_new(NULL, "AudioReceiverApp", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);
    if (!audio_output) {
        fprintf(stderr, "PulseAudio output error: %s\n", pa_strerror(error));
        // 프레임버퍼 자원 해제
        if (fbp != MAP_FAILED) munmap(fbp, screensize);
        close(fb_fd);
        return 1;
    }
    printf("PulseAudio playback stream initialized.\n");

    // 3. 소켓 초기화
    if((ssock = socket(AF_INET,SOCK_STREAM, 0))<0){
        perror("socket()");
        if (fbp != MAP_FAILED) munmap(fbp, screensize);
        close(fb_fd);
        if (audio_output) pa_simple_free(audio_output);
        return -1;
    }

    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);

    if(bind(ssock,(struct sockaddr *)&servaddr,sizeof(servaddr))<0){
        perror("bind()");
        close(ssock);
        if (fbp != MAP_FAILED) munmap(fbp, screensize);
        close(fb_fd);
        if (audio_output) pa_simple_free(audio_output);
        return -1;
    }

    if(listen(ssock,8)<0){
        perror("listen()");
        close(ssock);
        if (fbp != MAP_FAILED) munmap(fbp, screensize);
        close(fb_fd);
        if (audio_output) pa_simple_free(audio_output);
        return -1;
    }

    clen = sizeof(cliaddr);
    int csock;
    printf("Server listening on port %d\n", TCP_PORT);

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

        int flags = fcntl(csock, F_GETFL, 0);
        fcntl(csock, F_SETFL, flags | O_NONBLOCK);
        inet_ntop(AF_INET, &cliaddr.sin_addr,mesg,BUFSIZ);
        printf("Client is connected : %s\n",mesg);

        while (1) {
            char data_type; // 데이터 타입을 저장할 변수 (1바이트)
            int totalsize = 0; // 클라이언트가 보낼 데이터의 총 크기
            fd_set readfds;
            struct timeval tv;
            tv.tv_sec = 5;  // 5초 타임아웃
            tv.tv_usec = 0;

            FD_ZERO(&readfds);
            FD_SET(csock, &readfds);

            // select()를 사용하여 클라이언트에서 데이터가 도착할 때까지 효율적으로 대기
            int activity = select(csock + 1, &readfds, NULL, NULL, &tv);
            if (activity < 0) {
                perror("select()");
                break;
            } else if (activity == 0) {
                // 타임아웃 발생
                printf("Timeout waiting for client data\n");
                continue;
            }

            // 1. 데이터 타입 (1바이트) 수신
            int recv_result_type = recv_all(csock, &data_type, sizeof(data_type), 0);
            if (recv_result_type <= 0) {
                if (recv_result_type < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    usleep(1000); continue;
                }
                printf("Client disconnected or error during type reception\n");
                break; // 클라이언트 연결 종료
            }

            // 1. 데이터 타입 (1바이트) 수신
            if (recv_all(csock, &data_type, sizeof(data_type)) < 0) { // 마지막 0 인자 제거
                printf("Client disconnected or error during type reception\n");
                break; // 클라이언트 연결 종료
            }

            // 2. 데이터 크기 (4바이트) 수신
            if (recv_all(csock, &totalsize, sizeof(totalsize)) < 0) { // 마지막 0 인자 제거
                printf("Client disconnected or error during size reception\n");
                break; // 클라이언트 연결 종료
            }

            printf("Received data type: %d, expected size: %d bytes\n", data_type, totalsize);

            // 데이터 수신을 위한 버퍼 할당
            char *buffer = (char*)malloc(totalsize);
            if (!buffer) {
                perror("malloc() failed");
                break;
            }

            // 3. 실제 데이터 본문 수신
            if (recv_all(csock, buffer, totalsize) < 0) {
                perror("recv() buffer data failed"); // 에러 메시지 명확히 수정
                free(buffer);
                goto end_client_session;
            }

            // 모든 데이터를 성공적으로 받았는지 확인
            if (received == totalsize) {
                // 데이터 타입에 따라 처리 분기
                if (data_type == VIDEO_TYPE) {
                    // 비디오 데이터 처리 (기존 로직 유지)
                    display_frame(fbp, (uint8_t *)buffer, WIDTH, HEIGHT);
                    printf("Frame displayed on framebuffer successfully.\n");
                } else if (data_type == AUDIO_TYPE) {
                    // 오디오 데이터 처리
                    if (pa_simple_write(audio_output, buffer, totalsize, &error) < 0) {
                        fprintf(stderr, "PulseAudio write error: %s\n", pa_strerror(error));
                    } else {
                        printf("Audio data received and played successfully.\n");
                    }
                } else {
                    printf("Unknown data type received: %d\n", data_type);
                }

                // 클라이언트에게 최종 완료 응답 보내기 (성공: 1)
                int final_ack = 1;
                if (send(csock, &final_ack, sizeof(final_ack), 0) <= 0) {
                    perror("send() final ack");
                    free(buffer);
                    goto end_client_session;
                }
            } else {
                printf("Failed to receive complete data (%d/%d bytes)\n", received, totalsize);

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
            printf("Memory freed for this frame/audio.\n");
        }
    }

end_client_session:
    // 클라이언트 소켓 닫기
    close(csock);
    printf("Client disconnected.\n");

    // 메인 서버 소켓 및 자원 해제
    close(ssock);
    if (fbp != MAP_FAILED) munmap(fbp, screensize); // 프레임버퍼 매핑 해제
    close(fb_fd); // 프레임버퍼 파일 디스크립터 닫기
    if (audio_output) pa_simple_free(audio_output); // PulseAudio 스트림 해제
    printf("Server shutdown.\n");

    return 0;
}