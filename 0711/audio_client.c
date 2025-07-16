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

// send_all 함수 (앞서 정의된 내용과 동일)
int send_all(int sock, const void *buffer, size_t len) {
  size_t total_sent = 0;
  const char *buf = (const char *)buffer;

  while (total_sent < len) {
    ssize_t sent = send(sock, buf + total_sent, len - total_sent, 0);
    if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(1000);
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


#define SAMPLE_RATE 44100
#define CHANNELS    2
#define BUFFER_SIZE 12288 

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5100

#define AUDIO_TYPE 1

typedef struct {
  pa_simple *input;
  int client_socket;
} audio_data_t;

void *capture_and_send(void *data) {
  audio_data_t *audio_data = (audio_data_t *)data;
  pa_simple *input = audio_data->input;
  int client_socket = audio_data->client_socket;
  int error;
  char buffer[BUFFER_SIZE];
  int bytes_to_send = BUFFER_SIZE;
  char data_type = AUDIO_TYPE;

  while (1) {
    //총 음성 버퍼 읽기
    if (pa_simple_read(input, buffer, sizeof(buffer), &error) < 0) {
      fprintf(stderr, "PulseAudio read error: %s\n", pa_strerror(error));
      break;
    }
    //헤더 보내기
    if (send_all(client_socket, &data_type, sizeof(data_type)) < 0) {
      fprintf(stderr, "Failed to send data type\n");
      break;
    }
    //본체 크기 알려주기
    if (send_all(client_socket, &bytes_to_send, sizeof(bytes_to_send)) < 0) {
      fprintf(stderr, "Failed to send audio size\n");
      break;
    }
    //본체 보내기 
    if (send_all(client_socket, buffer, sizeof(buffer)) < 0) {
      fprintf(stderr, "Failed to send audio data\n");
      break;
    }
  }

  return NULL;
}

int main(int argc, char **argv) {
    pa_simple *input = NULL;
    pa_sample_spec ss;

    pthread_t thread;
    audio_data_t audio_data;
    int error;
    int client_socket;
    struct sockaddr_in server_addr;
    //직접 지정
    const char *specific_mic_source = "alsa_input.usb-GeneralPlus_USB_Audio_Device-00.mono-fallback";.

    // PulseAudio 샘플 사양 설정
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = 44100;
    ss.channels = 1;  // 모노 채널로 변경

    // PulseAudio 입력(캡처) 스트림 생성 시 특정 마이크 소스 지정
    // specific_mic_source를 NULL 대신 실제 마이크 이름으로 변경
    input = pa_simple_new(NULL, "FullDuplexApp", PA_STREAM_RECORD, specific_mic_source, "record", &ss, NULL, NULL, &error);
    if (!input) {
        fprintf(stderr, "PulseAudio input error: %s\n", pa_strerror(error));
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