#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pthread.h>

#define SAMPLE_RATE 44100
#define CHANNELS    2
#define BUFFER_SIZE 4096

typedef struct {
  pa_simple *input;
  pa_simple *output;
} audio_data_t;

void *capture_and_playback(void *data)
{
  audio_data_t *audio_data = (audio_data_t *)data;
  pa_simple *input = audio_data->input;
  pa_simple *output = audio_data->output;
  int error;
  char buffer[BUFFER_SIZE];

  while (1) {
    // 오디오 캡처
    if (pa_simple_read(input, buffer, sizeof(buffer), &error) < 0) {
      fprintf(stderr, "PulseAudio read error: %s\n", pa_strerror(error));
      break;
    }

    // 캡처한 오디오를 재생
    if (pa_simple_write(output, buffer, sizeof(buffer), &error) < 0) {
      fprintf(stderr, "PulseAudio write error: %s\n", pa_strerror(error));
      break;
    }
  }

  return NULL;
}

int main(int argc, char** argv) 
{
  pa_simple *input = NULL;
  pa_simple *output = NULL;
  pa_sample_spec ss;
  pthread_t thread;
  audio_data_t audio_data;
  int error;

  // 샘플 사양 설정 (16비트, 2채널, 44100Hz)
  ss.format = PA_SAMPLE_S16LE;
  ss.rate = SAMPLE_RATE;
  ss.channels = CHANNELS;

  // PulseAudio 입력(캡처) 스트림 생성
  input = pa_simple_new(NULL, "FullDuplexApp", PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error);
  if (!input) {
    fprintf(stderr, "PulseAudio input error: %s\n", pa_strerror(error));
    return 1;
  }

  // PulseAudio 출력(재생) 스트림 생성
  output = pa_simple_new(NULL, "FullDuplexApp", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);
  if (!output) {
    fprintf(stderr, "PulseAudio output error: %s\n", pa_strerror(error));
    if (input) pa_simple_free(input);
    return 1;
  }

  // 캡처 및 재생을 위한 구조체에 스트림 핸들 저장
  audio_data.input = input;
  audio_data.output = output;

  // 캡처 및 재생 스레드 시작
  pthread_create(&thread, NULL, capture_and_playback, &audio_data);

  // 스레드가 끝날 때까지 기다림
  pthread_join(thread, NULL);

  // PulseAudio 스트림 닫기
  if (input) pa_simple_free(input);
  if (output) pa_simple_free(output);

  return 0;
}