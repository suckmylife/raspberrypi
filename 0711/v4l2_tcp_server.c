#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>    // select() 함수를 위한 헤더
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>      // 프레임버퍼를 위한 헤더
#include <linux/videodev2.h> // 비디오 장치를 위한 헤더
#include <pulse/simple.h>  // PulseAudio를 위한 헤더
#include <pulse/error.h>   // PulseAudio 에러를 위한 헤더

// --- 상수 정의 ---
#define TCP_PORT 5100
#define WIDTH 640
#define HEIGHT 480
#define FRAMEBUFFER_DEVICE "/dev/fb0"

#define VIDEO_TYPE 0 // 비디오 데이터 타입
#define AUDIO_TYPE 1 // 오디오 데이터 타입

// --- 전역 변수 (프레임버퍼 및 PulseAudio 스트림) ---
static struct fb_var_screeninfo vinfo;
static uint16_t *fbp = NULL; // 프레임버퍼 매핑 포인터
static pa_simple *audio_output = NULL; // PulseAudio 출력 스트림

// --- 유틸리티 함수: 정확히 len 바이트를 모두 받을 때까지 반복 ---
int recv_all(int sock, void *buffer, size_t len) {
    size_t total = 0;
    char *buf = (char *)buffer;
    while (total < len) {
        ssize_t n = recv(sock, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // 1ms 대기 후 재시도
                continue;
            } else {
                perror("recv() error");
                return -1; // 치명적인 오류 발생
            }
        } else if (n == 0) {
            // 클라이언트 연결 종료
            return -1;
        }
        total += n;
    }
    return 0; // 성공적으로 모든 바이트 수신
}

// --- 유틸리티 함수: 정확히 len 바이트를 모두 보낼 때까지 반복 ---
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
        } else if (sent == 0) {
            // 연결이 닫힌 경우
            return -1;
        }
        total_sent += sent;
    }
    return 0; // 성공적으로 모든 바이트 전송
}

// --- 비디오 프레임 디스플레이 함수 (기존 코드 유지) ---
void display_frame(uint16_t *fbp_ptr, uint8_t *data, int width, int height) {
    int x_offset = (vinfo.xres - width) / 2;
    int y_offset = (vinfo.yres - height) / 2;

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

            uint16_t pixel1 = ((R1 & 0xF8) << 8) | ((G1 & 0xFC) << 3) | (B1 >> 3);
            uint16_t pixel2 = ((R2 & 0xF8) << 8) | ((G2 & 0xFC) << 3) | (B2 >> 3);

            fbp_ptr[(y + y_offset) * vinfo.xres + (x + x_offset)] = pixel1;
            fbp_ptr[(y + y_offset) * vinfo.xres + (x + x_offset + 1)] = pixel2;
        }
    }
}

// --- 메인 함수 ---
int main() {
    int listener, newfd;             // listener: 연결 대기 소켓, newfd: 새 클라이언트 소켓
    int fdmax;                       // master_set에 있는 최대 파일 디스크립터 번호
    struct sockaddr_in servaddr, cliaddr; // 서버 및 클라이언트 주소 구조체
    socklen_t addrlen;
    fd_set master_set;               // 모든 활성 소켓(listener + 클라이언트)의 집합
    fd_set read_fds;                 // select()가 반환하는, 읽기 가능한 소켓의 집합
    char mesg_ip[INET_ADDRSTRLEN];   // 클라이언트 IP 주소 저장용 버퍼

    // 1. 프레임버퍼 초기화
    int fb_fd = open(FRAMEBUFFER_DEVICE, O_RDWR);
    if (fb_fd == -1) {
        perror("Error opening framebuffer device");
        exit(EXIT_FAILURE);
    }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information");
        close(fb_fd);
        exit(EXIT_FAILURE);
    }
    uint32_t screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if ((intptr_t)fbp == -1) {
        perror("Error mapping framebuffer device to memory");
        close(fb_fd);
        exit(EXIT_FAILURE);
    }
    printf("Framebuffer initialized.\n");

    // 2. PulseAudio 초기화 (서버에서 오디오 재생용)
    pa_sample_spec ss_pa;
    int pa_error;
    ss_pa.format = PA_SAMPLE_S16LE;
    ss_pa.rate = 44100; // 오디오 클라이언트와 동일하게 설정
    ss_pa.channels = 2; // 오디오 클라이언트와 동일하게 설정 (모노 마이크의 경우 1)
    audio_output = pa_simple_new(NULL, "AudioReceiverApp", PA_STREAM_PLAYBACK, NULL, "playback", &ss_pa, NULL, NULL, &pa_error);
    if (!audio_output) {
        fprintf(stderr, "PulseAudio output error: %s\n", pa_strerror(pa_error));
        munmap(fbp, screensize); close(fb_fd);
        exit(EXIT_FAILURE);
    }
    printf("PulseAudio playback stream initialized.\n");

    // 3. 리스너 소켓 생성 및 설정
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket() creation failed");
        pa_simple_free(audio_output); munmap(fbp, screensize); close(fb_fd);
        exit(EXIT_FAILURE);
    }

    // 소켓 재사용 옵션 설정 (서버 재시작 시 포트 바로 사용 가능하도록)
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // 모든 네트워크 인터페이스에서 연결 허용
    servaddr.sin_port = htons(TCP_PORT);

    if (bind(listener, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind() failed");
        close(listener); pa_simple_free(audio_output); munmap(fbp, screensize); close(fb_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(listener, 10) < 0) { // 최대 10개의 대기 연결 허용
        perror("listen() failed");
        close(listener); pa_simple_free(audio_output); munmap(fbp, screensize); close(fb_fd);
        exit(EXIT_FAILURE);
    }

    // 4. select()를 위한 fd_set 초기화
    FD_ZERO(&master_set); // master_set을 0으로 초기화 (모든 비트를 끔)
    FD_SET(listener, &master_set); // 리스너 소켓을 master_set에 추가 (항상 감시)
    fdmax = listener; // 초기 fdmax는 리스너 소켓 번호

    printf("Server listening on port %d...\n", TCP_PORT);

    // --- 메인 이벤트 루프 ---
    while (1) {
        read_fds = master_set; // select()가 변경할 수 있으므로 master_set을 read_fds에 복사
        // select() 호출: fdmax+1까지의 소켓들을 read_fds에서 감시. NULL은 타임아웃 없음 (무한 대기).
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select() failed");
            // select 실패 시 복구 불가능한 에러일 가능성이 높으므로 종료
            break;
        }

        // 모든 활성 파일 디스크립터를 순회하며 이벤트가 발생한 소켓 찾기
        for (int i = 0; i <= fdmax; i++) { // i: 현재 확인 중인 파일 디스크립터 번호
            if (FD_ISSET(i, &read_fds)) { // i번 소켓에서 이벤트(읽기 가능)가 발생했는지 확인
                if (i == listener) {
                    // 리스너 소켓에 이벤트 발생 = 새 클라이언트 연결 요청
                    addrlen = sizeof(cliaddr);
                    newfd = accept(listener, (struct sockaddr *)&cliaddr, &addrlen);
                    if (newfd == -1) {
                        perror("accept() failed");
                    } else {
                        // 새 클라이언트 소켓을 master_set에 추가
                        FD_SET(newfd, &master_set);
                        // fdmax 갱신 (새로 추가된 소켓 번호가 더 크면 업데이트)
                        if (newfd > fdmax) {
                            fdmax = newfd;
                        }
                        inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_ip, sizeof(mesg_ip));
                        printf("New connection from %s on socket %d\n", mesg_ip, newfd);
                    }
                } else {
                    // 기존 클라이언트 소켓 (i) 에 이벤트 발생 = 데이터 도착 또는 연결 종료
                    char data_type;
                    int totalsize;
                    char *buffer = NULL; // 동적 할당될 버퍼 포인터

                    // --- 데이터 수신 단계 ---
                    // 1. 데이터 타입(1바이트) 수신
                    if (recv_all(i, &data_type, sizeof(data_type)) < 0) {
                        // 오류 발생 또는 클라이언트 연결 종료
                        printf("Client disconnected or error during type reception on socket %d.\n", i);
                        goto client_cleanup; // 이 클라이언트 세션 정리로 이동
                    }

                    // 2. 데이터 크기(4바이트) 수신
                    if (recv_all(i, &totalsize, sizeof(totalsize)) < 0) {
                        printf("Client disconnected or error during size reception on socket %d.\n", i);
                        goto client_cleanup; // 이 클라이언트 세션 정리로 이동
                    }
                    printf("Socket %d: Received data type %d, expected size %d bytes.\n", i, data_type, totalsize);

                    // 3. 데이터 본문 수신을 위한 버퍼 할당
                    buffer = (char*)malloc(totalsize);
                    if (!buffer) {
                        perror("malloc() failed for data buffer");
                        goto client_cleanup; // 이 클라이언트 세션 정리로 이동
                    }

                    // 4. 데이터 본문 수신
                    if (recv_all(i, buffer, totalsize) < 0) {
                        perror("recv_all() for data body failed");
                        goto client_cleanup; // 이 클라이언트 세션 정리로 이동
                    }

                    // --- 데이터 처리 단계 ---
                    int final_ack = 1; // 기본적으로 성공으로 가정

                    if (data_type == VIDEO_TYPE) {
                        // 비디오 데이터 처리
                        display_frame(fbp, (uint8_t *)buffer, WIDTH, HEIGHT);
                        // printf("Frame displayed from socket %d.\n", i);
                    } else if (data_type == AUDIO_TYPE) {
                        // 오디오 데이터 처리
                        int current_pa_error;
                        if (pa_simple_write(audio_output, buffer, totalsize, &current_pa_error) < 0) {
                            fprintf(stderr, "PulseAudio write error on socket %d: %s\n", i, pa_strerror(current_pa_error));
                            final_ack = 0; // 재생 실패 시 클라이언트에게 실패 알림
                        } else {
                            // printf("Audio data played from socket %d.\n", i);
                        }
                    } else {
                        // 알 수 없는 데이터 타입
                        printf("Unknown data type (%d) received from socket %d.\n", data_type, i);
                        final_ack = 0; // 알 수 없는 타입이므로 실패 알림
                    }

                    // --- 클라이언트에게 완료 응답 전송 ---
                    if (send_all(i, &final_ack, sizeof(final_ack)) < 0) { // send_all 사용
                        perror("send_all() final ack failed");
                        goto client_cleanup; // 응답 전송 실패 시 정리
                    }

                    // --- 현재 클라이언트 처리 완료 ---
                    free(buffer); // 할당된 버퍼 해제
                    // 이 클라이언트 소켓에 대한 현재 데이터 처리가 성공적으로 완료됨.
                    // 다음 select() 루프에서 다른 데이터가 오기를 기다림.
                    continue; // 현재 루프의 나머지 코드를 건너뛰고 다음 i로 넘어감 (선택 사항)

                client_cleanup: // 이 클라이언트 소켓의 자원만 해제
                    if (buffer) { // malloc된 buffer가 있다면 해제
                        free(buffer);
                    }
                    printf("Cleaning up client socket %d.\n", i);
                    close(i); // 클라이언트 소켓 닫기
                    FD_CLR(i, &master_set); // master_set에서 제거하여 더 이상 감시하지 않음
                    // fdmax가 닫힌 소켓 번호였으면 갱신 (선택 사항이지만, 안정성을 높임)
                    if (i == fdmax) {
                        while (FD_ISSET(fdmax, &master_set) == 0) {
                            fdmax--;
                        }
                    }
                } // else (i != listener) 끝
            } // if (FD_ISSET(i, &read_fds)) 끝
        } // for 루프 끝
    } // while(1) (메인 이벤트 루프) 끝

    // --- 서버 종료 시 자원 해제 (루프가 깨졌을 때만 실행) ---
    close(listener); // 리스너 소켓 닫기

    // 모든 남아있는 클라이언트 소켓들도 닫기
    for (int i = 0; i <= fdmax; i++) {
        if (FD_ISSET(i, &master_set) && i != listener) {
            close(i);
        }
    }

    if (fbp != MAP_FAILED) {
        munmap(fbp, screensize); // 프레임버퍼 매핑 해제
    }
    close(fb_fd); // 프레임버퍼 파일 디스크립터 닫기

    if (audio_output) {
        pa_simple_free(audio_output); // PulseAudio 스트림 해제
    }
    printf("Server shutdown.\n");

    return 0;
}