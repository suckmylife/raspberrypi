#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pthread.h>
#include <sys/socket.h> // 소켓 통신을 위한 헤더
#include <arpa/inet.h>  // 인터넷 주소 변환을 위한 헤더
#include <unistd.h>     // close 함수를 위한 헤더

#define SAMPLE_RATE 44100
#define CHANNELS    2
#define BUFFER_SIZE 4096 // 오디오 버퍼 크기

#define SERVER_IP "127.0.0.1" // 서버 IP 주소 (예: 로컬호스트)
#define SERVER_PORT 5100      // server.txt와 동일한 포트

#define AUDIO_TYPE 1 // 오디오 데이터임을 나타내는 타입 (1바이트)

typedef struct {
  pa_simple *input;
  int client_socket; // 서버로 데이터를 보낼 소켓 디스크립터 추가
} audio_data_t;

void *capture_and_send(void *data)
{
  audio_data_t *audio_data = (audio_data_t *)data;
  pa_simple *input = audio_data->input;
  int client_socket = audio_data->client_socket;
  int error;
  char buffer[BUFFER_SIZE];
  int bytes_to_send = BUFFER_SIZE; // 전송할 오디오 데이터의 크기
  char data_type = AUDIO_TYPE; // 전송할 데이터 타입

  while (1) {
    // 오디오 캡처
    if (pa_simple_read(input, buffer, sizeof(buffer), &error) < 0) {
      fprintf(stderr, "PulseAudio read error: %s\n", pa_strerror(error));
      break;
    }

    // 1. 데이터 타입 (1바이트) 전송
    if (send(client_socket, &data_type, sizeof(data_type), 0) < 0) {
        perror("send() data type failed");
        break;
    }

    // 2. 캡처한 오디오 데이터의 크기 (4바이트)를 먼저 전송
    if (send(client_socket, &bytes_to_send, sizeof(bytes_to_send), 0) < 0) {
        perror("send() audio size failed");
        break;
    }

    // 3. 캡처한 오디오 데이터를 서버로 전송
    if (send(client_socket, buffer, sizeof(buffer), 0) < 0) {
      perror("send() audio data failed");
      break;
    }
    //printf("Sent %d bytes of audio data.\n", bytes_to_send); // 디버깅용
  }

  return NULL;
}

int main(int argc, char** argv)
{
  pa_simple *input = NULL;
  pa_sample_spec ss;
  pthread_t thread;
  audio_data_t audio_data;
  int error;
  int client_socket;
  struct sockaddr_in server_addr;

  // 샘플 사양 설정 (16비트, 2채널, 44100Hz)
  ss.format = PA_SAMPLE_S16LE;
  ss.rate = SAMPLE_RATE;
  ss.channels = CHANNELS;

  // PulseAudio 입력(캡처) 스트림 생성
  input = pa_simple_new(NULL, "AudioSenderApp", PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error);
  if (!input) {
    fprintf(stderr, "PulseAudio input error: %s\n", pa_strerror(error));
    return 1;
  }

  // 서버에 연결할 소켓 생성
  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket < 0) {
    perror("socket creation failed");
    if (input) pa_simple_free(input);
    return 1;
  }

  // 서버 주소 설정
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
    perror("Invalid address/ Address not supported");
    close(client_socket);
    if (input) pa_simple_free(input);
    return 1;
  }

  // 서버에 연결
  if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connection failed");
    close(client_socket);
    if (input) pa_simple_free(input);
    return 1;
  }
  printf("Connected to server at %s:%d\n", SERVER_IP, SERVER_PORT);

  // 캡처 및 전송을 위한 구조체에 스트림 핸들 및 소켓 핸들 저장
  audio_data.input = input;
  audio_data.client_socket = client_socket;

  // 캡처 및 전송 스레드 시작
  pthread_create(&thread, NULL, capture_and_send, &audio_data);

  // 스레드가 끝날 때까지 기다림
  pthread_join(thread, NULL);

  // PulseAudio 스트림 닫기
  if (input) pa_simple_free(input);
  // 소켓 닫기
  close(client_socket);

  return 0;
}