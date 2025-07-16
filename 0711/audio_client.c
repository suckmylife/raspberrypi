#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h> // select() 사용을 위한 헤더
#include <sys/select.h> // select() 사용을 위한 헤더


#define SAMPLE_RATE 44100
#define CHANNELS    1 // 혹은 1 (마이크에 따라 모노/스테레오 설정)
#define BUFFER_SIZE 12288 // 버퍼 크기 (4096 * 3배)

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5100

#define AUDIO_TYPE 1

typedef struct {
  pa_simple *input;
  int client_socket;
} audio_data_t;

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

// recv()를 반복 호출하여 정확히 len 바이트를 모두 받는 함수
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
                return -1;
            }
        } else if (n == 0) {
            printf("Connection closed by peer during recv_all\n");
            return -1;
        }
        total += n;
    }
    return 0;
}

void *capture_and_send(void *data) {
  audio_data_t *audio_data = (audio_data_t *)data;
  int client_socket = audio_data->client_socket; // 소켓은 여기로 옮겨서 사용
  pa_simple *input = audio_data->input; // input 스트림도 마찬가지

  int error;
  char buffer[BUFFER_SIZE];
  int bytes_to_send = BUFFER_SIZE;
  char data_type = AUDIO_TYPE;

  while (1) {
    // 1. 오디오 데이터 캡처
    if (pa_simple_read(input, buffer, sizeof(buffer), &error) < 0) {
      fprintf(stderr, "PulseAudio read error: %s\n", pa_strerror(error));
      break;
    }

    // 2. 데이터 타입 전송
    if (send_all(client_socket, &data_type, sizeof(data_type)) < 0) {
      fprintf(stderr, "Failed to send data type\n");
      break;
    }

    // 3. 오디오 데이터 크기 전송
    if (send_all(client_socket, &bytes_to_send, sizeof(bytes_to_send)) < 0) {
      fprintf(stderr, "Failed to send audio size\n");
      break;
    }

    // 4. 실제 오디오 데이터 전송
    if (send_all(client_socket, buffer, sizeof(buffer)) < 0) {
      fprintf(stderr, "Failed to send audio data\n");
      break;
    }

    // 5. 서버로부터 최종 완료 응답 대기 및 수신
    int final_server_response;
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);
    tv.tv_sec = 5;  // 5초 타임아웃
    tv.tv_usec = 0;

    int activity = select(client_socket + 1, &readfds, NULL, NULL, &tv);

    if (activity < 0) {
        perror("select() for server response");
        break; // select 오류 발생
    } else if (activity == 0) {
        // 타임아웃 발생 (서버 응답을 5초 내에 받지 못함)
        printf("Timeout waiting for server response after sending audio data.\n");
        break; // 루프 종료 또는 재시도 로직 추가
    }

    // 데이터가 도착했으므로 recv_all()을 사용하여 최종 응답 수신
    if (recv_all(client_socket, &final_server_response, sizeof(final_server_response)) < 0) {
        perror("recv_all() final server response");
        break; // 수신 오류 발생
    }

    // 서버의 확인 응답에 따라 처리
    if (final_server_response == 1) {
        printf("Audio chunk sent and confirmed by server.\n");
        // 성공 시 다음 오디오 블록 전송을 위해 계속 루프
    } else {
        fprintf(stderr, "Server indicated failure to receive audio data.\n");
        break; // 서버가 실패를 알렸으므로 루프 종료
    }
  }

  return NULL; // 스레드 종료
}

int main(int argc, char **argv) {
  pa_simple *input = NULL;
  pa_sample_spec ss;
  pthread_t thread;
  audio_data_t audio_data;
  int error;
  int client_socket;
  struct sockaddr_in server_addr;

  // 특정 마이크 소스 지정 (예: "alsa_input.usb-GeneralPlus_USB_Audio_Device-00.analog-stereo")
  // 1. pactl list short sources 명령어로 정확한 이름을 확인하고 여기에 넣어주세요.
  // 2. 만약 모노 마이크라면 CHANNELS를 1로 변경했는지 다시 확인해주세요.
  const char *specific_mic_source = "alsa_input.usb-GeneralPlus_USB_Audio_Device-00.mono-fallback"; // 실제 이름으로 교체

  // PulseAudio 샘플 사양 설정
  ss.format = PA_SAMPLE_S16LE;
  ss.rate = SAMPLE_RATE;
  ss.channels = CHANNELS;

  // PulseAudio 입력(캡처) 스트림 생성
  input = pa_simple_new(NULL, "FullDuplexApp", PA_STREAM_RECORD, specific_mic_source, "record", &ss, NULL, NULL, &error);
  if (!input) {
    fprintf(stderr, "PulseAudio input error: %s\n", pa_strerror(error));
    // 추가 힌트: "no such entity" 오류는 specific_mic_source 이름이 잘못된 경우 발생
    // pulseaudio --start 명령어로 PulseAudio 데몬이 실행 중인지 확인
    return 1;
  }
  printf("PulseAudio input stream initialized with source: %s\n", specific_mic_source ? specific_mic_source : "default");

  // 소켓 생성
  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket < 0) {
    perror("socket creation failed");
    pa_simple_free(input);
    return 1;
  }

  // 서버 주소 설정
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
    perror("Invalid address");
    close(client_socket);
    pa_simple_free(input);
    return 1;
  }

  // 서버 연결
  if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connection failed");
    close(client_socket);
    pa_simple_free(input);
    return 1;
  }
  printf("Connected to server at %s:%d\n", SERVER_IP, SERVER_PORT);

  // 구조체에 스트림과 소켓 저장
  audio_data.input = input;
  audio_data.client_socket = client_socket;

  // 캡처 및 전송 스레드 시작
  pthread_create(&thread, NULL, capture_and_send, &audio_data);

  // 스레드 종료 대기
  pthread_join(thread, NULL);

  // 자원 해제
  pa_simple_free(input);
  close(client_socket);

  return 0;
}