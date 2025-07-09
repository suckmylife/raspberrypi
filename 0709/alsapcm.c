// #include <stdio.h>
// #include <limits.h>
// #include <math.h>
// #include <alsa/asoundlib.h>

// #define BITS 2
// #define FRAGEMENT 8
// #define DURATION 5.0
// #define MODE 1
// #define FREQ 44100
// #define BUFSIZE (int)(BITS*FREQ*DURATION*MODE)

// int setupDSP(snd_pcm_t *dev, int buf_size, int format, int sampleRate, int channels);

// int main(int argc, char **argv)
// {
//     snd_pcm_t *playback_handle;
//     double total = DURATION, t;
//     int freq = 440;
//     int i, frames, count = 1;
//     char *snd_dev_out = "plughw:0,0";
//     //char *snd_dev_out = "default";

//     short buf[BUFSIZE];

//     if(snd_pcm_open(&playback_handle,snd_dev_out,SND_PCM_STREAM_PLAYBACK,0) < 0){
//         perror("Could not open output audio dev");
//         return -1;
//     }
//     if(setupDSP(playback_handle,BUFSIZE,SND_PCM_FORMAT_S16_LE, FREQ,MODE)<0){
//         perror("Could not set output audio device");
//         return -1;
//     }

//     printf("MAKE SINE WAVE!!\n");
//     for(i = 0; i < BUFSIZE; i++){
//         t = (total/BUFSIZE) * i;
//         buf[i] = SHRT_MAX * sin((2.0*M_PI*freq*t));
//     }
//     frames = BUFSIZE / (MODE * BITS);

//     while(count--){
//         snd_pcm_prepare(playback_handle);
//         snd_pcm_writei(playback_handle,buf,frames);
//     }
//     snd_pcm_drop(playback_handle);
//     snd_pcm_close(playback_handle);
//     return 0;
// }

// int setupDSP(snd_pcm_t *dev, int buf_size, int format, int sampleRate, int channels)
// {
//     snd_pcm_hw_params_t* hw_params;
//     snd_pcm_uframes_t frames;
//     int fragments = FRAGEMENT;
//     int bits = (format == SND_PCM_FORMAT_S16_LE)?2:1;

//     if(snd_pcm_hw_params_malloc(&hw_params)<0){
//         perror("could not allocate parameter");
//         return -1;
//     }
//     if(snd_pcm_hw_params_any(dev,hw_params)<0){
//         perror("could not initialize parameter");
//         return -1;
//     }
    
//     if(snd_pcm_hw_params_set_access(dev,hw_params,SND_PCM_ACCESS_RW_INTERLEAVED)<0){
//         perror("could not set access type");
//         return -1;
//     }

//     if(snd_pcm_hw_params_set_format(dev,hw_params,format)<0){
//         perror("could not set format");
//         return -1;
//     }

//     if(snd_pcm_hw_params_set_rate_near(dev,hw_params,&sampleRate,0)<0){
//         perror("could not set sample rate");
//         return -1;
//     }

//     if(snd_pcm_hw_params_set_channels(dev,hw_params,channels)<0){
//         perror("could not set channels count");
//         return -1;
//     }

//     if(snd_pcm_hw_params_set_periods_near(dev,hw_params,&fragments,0)<0){
//         perror("could not set fragments");
//         return -1;
//     }
//     frames = (buf_size * fragments) / (channels * bits);
//     if(snd_pcm_hw_params_set_buffer_size_near(dev,hw_params,&frames)<0){
//         perror("could not set buffer size");
//         return -1;
//     }

//     buf_size = frames * channels * bits / fragments;

//     if(snd_pcm_hw_params(dev,hw_params)<0){
//         perror("could not set HW params");
//         return -1;
//     }
//     return 0;
// }
#include <stdio.h>
#include <limits.h>
#include <math.h>       // For sin() and M_PI
#include <alsa/asoundlib.h>
#include <unistd.h>     // For usleep()

// --- 오디오 설정 상수 ---
#define MODE 2          // 채널 수: 1 for mono, 2 for stereo (스테레오 권장)
#define FREQ 44100      // 샘플 레이트 (Hz)
#define BITS 2          // 샘플당 바이트 수 (S16_LE는 16비트 = 2바이트)
#define NOTE_DURATION_SEC 0.4 // 각 음표의 재생 시간 (초)
                            // 이 값을 조절하여 멜로디 재생 속도를 변경할 수 있습니다.

// 한 음표를 위한 버퍼 크기 계산 (프레임 단위)
// 1 프레임 = 1 샘플 * 채널 수
#define SAMPLES_PER_NOTE (int)(FREQ * NOTE_DURATION_SEC) // 한 음표당 샘플 수 (단일 채널 기준)
#define BUFFER_SIZE_TOTAL_SAMPLES (SAMPLES_PER_NOTE * MODE) // 전체 버퍼의 총 샘플 수 (인터리브드)

// --- 함수 프로토타입 ---
// DSP (Digital Signal Processor) 설정을 위한 함수
// dev: PCM 장치 핸들
// buffer_frames: ALSA에 설정할 버퍼 크기 (프레임 단위, 즉 채널당 샘플 수)
// format: 샘플 형식 (예: SND_PCM_FORMAT_S16_LE)
// sampleRate: 샘플 레이트
// channels: 채널 수
int setupDSP(snd_pcm_t *dev, snd_pcm_uframes_t buffer_frames, int format, int sampleRate, int channels);

int main(int argc, char **argv)
{
    snd_pcm_t *playback_handle; // PCM 재생 장치 핸들
    char *snd_dev_out = "plughw:0,0"; // 라즈베리파이 3.5mm 오디오 잭 (card 0, device 0)
                                      // 'aplay -l' 명령으로 확인한 값입니다.
                                      // 만약 소리가 나지 않으면 "default"로 변경하여 시도해 볼 수 있습니다.
                                      // char *snd_dev_out = "default";

    // --- 전역 버퍼 선언을 main 함수 내부로 이동하여 지역 변수로 선언 ---
    // 이렇게 하면 VLA (Variable Length Array) 경고가 사라집니다.
    short audio_buffer[BUFFER_SIZE_TOTAL_SAMPLES];

    // --- "Butterfly" 멜로디의 단순화된 음표 주파수 배열 (Hz) ---
    // 이 배열은 실제 노래의 일부 음표를 대략적으로 표현한 것입니다.
    // 각 음표는 NOTE_DURATION_SEC 만큼 재생됩니다.
    double melody_freqs[] = {
        392.0, // G4
        440.0, // A4
        494.0, // B4
        392.0, // G4
        523.0, // C5
        494.0, // B4
        440.0, // A4
        392.0  // G4
    };
    int num_notes = sizeof(melody_freqs) / sizeof(melody_freqs[0]); // 배열의 음표 개수

    // --- ALSA PCM 장치 열기 ---
    if (snd_pcm_open(&playback_handle, snd_dev_out, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        perror("Error: Could not open output audio device");
        return -1;
    }

    // --- DSP (오디오 장치) 설정 ---
    // setupDSP 함수에 SAMPLES_PER_NOTE (프레임 수)를 전달합니다.
    if (setupDSP(playback_handle, SAMPLES_PER_NOTE, SND_PCM_FORMAT_S16_LE, FREQ, MODE) < 0) {
        perror("Error: Could not set output audio device parameters");
        snd_pcm_close(playback_handle); // 오류 발생 시 장치 닫기
        return -1;
    }

    printf("Playing a simplified 'Butterfly' melody (Digimon Adventure OST)...\n");
    printf("Note duration: %.2f seconds per note.\n", NOTE_DURATION_SEC);

    // --- 멜로디 재생 루프 ---
    for (int note_idx = 0; note_idx < num_notes; note_idx++) {
        int current_freq = (int)melody_freqs[note_idx]; // 현재 재생할 음표의 주파수
        printf("  Playing note: %d Hz\n", current_freq);

        // --- 사인파 생성 ---
        // audio_buffer에 현재 음표의 사인파 데이터를 채웁니다.
        for (int i = 0; i < BUFFER_SIZE_TOTAL_SAMPLES; i++) {
            // 'i'는 버퍼의 샘플 인덱스입니다.
            // 인터리브드 모드(MODE=2)에서는 'i'가 짝수일 때 왼쪽 채널, 홀수일 때 오른쪽 채널입니다.
            // 't'는 현재 프레임의 시간(초)을 나타냅니다.
            // (i / MODE)는 현재 샘플이 속한 프레임의 인덱스입니다.
            double t = (double)(i / MODE) / FREQ;
            // SHRT_MAX는 short 타입이 가질 수 있는 최대값으로, 소리의 진폭을 결정합니다.
            audio_buffer[i] = SHRT_MAX * sin(2.0 * M_PI * current_freq * t);
        }

        // --- ALSA PCM 장치 준비 ---
        // 장치가 데이터를 받을 준비가 되었는지 확인합니다.
        if (snd_pcm_prepare(playback_handle) < 0) {
            perror("Warning: Could not prepare PCM device for note. Attempting to continue.");
            // 치명적인 오류가 아니므로 계속 진행합니다.
        }

        // --- 오디오 데이터 쓰기 ---
        // 생성된 사인파 데이터를 ALSA 장치로 보냅니다.
        // 세 번째 인자는 '프레임' 수입니다 (채널당 샘플 수).
        if (snd_pcm_writei(playback_handle, audio_buffer, SAMPLES_PER_NOTE) < 0) {
            perror("Warning: Could not write to PCM device for note. Attempting to continue.");
            // 치명적인 오류가 아니므로 계속 진행합니다.
        }

        // --- 음표 재생 시간만큼 대기 ---
        // 다음 음표를 재생하기 전에 현재 음표가 재생될 시간을 기다립니다.
        usleep((int)(NOTE_DURATION_SEC * 1000000)); // usleep은 마이크로초 단위 (1초 = 1,000,000 마이크로초)
    }

    // --- 재생 완료 후 장치 정리 ---
    snd_pcm_drain(playback_handle); // 모든 버퍼링된 데이터가 재생될 때까지 기다립니다.
    snd_pcm_close(playback_handle); // PCM 장치를 닫습니다.
    printf("Melody playback finished.\n");

    return 0;
}

// --- setupDSP 함수 구현 ---
// ALSA PCM 장치의 하드웨어 파라미터를 설정합니다.
int setupDSP(snd_pcm_t *dev, snd_pcm_uframes_t buffer_frames, int format, int sampleRate, int channels)
{
    snd_pcm_hw_params_t* hw_params; // 하드웨어 파라미터 구조체
    snd_pcm_uframes_t actual_buffer_frames; // 실제 설정될 버퍼 프레임 수를 저장할 변수
    unsigned int actual_sample_rate; // 실제 설정될 샘플 레이트를 저장할 변수
    int fragments = 4; // 버퍼 조각(periods) 수. 일반적으로 2, 4, 8 등.

    // 하드웨어 파라미터 구조체 할당
    if (snd_pcm_hw_params_malloc(&hw_params) < 0) {
        perror("Error: could not allocate hardware parameters");
        return -1;
    }
    // 모든 파라미터를 기본값으로 초기화
    if (snd_pcm_hw_params_any(dev, hw_params) < 0) {
        perror("Error: could not initialize hardware parameters");
        snd_pcm_hw_params_free(hw_params); // 할당 해제
        return -1;
    }

    // 접근 방식 설정: 인터리브드 모드 (좌우 채널 샘플이 번갈아 저장됨)
    if (snd_pcm_hw_params_set_access(dev, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        perror("Error: could not set access type");
        snd_pcm_hw_params_free(hw_params);
        return -1;
    }

    // 샘플 형식 설정 (예: 16비트 리틀 엔디언 부호 있는 정수)
    if (snd_pcm_hw_params_set_format(dev, hw_params, format) < 0) {
        perror("Error: could not set format");
        snd_pcm_hw_params_free(hw_params);
        return -1;
    }

    // 샘플 레이트 설정 (가장 가까운 값으로 설정될 수 있음)
    actual_sample_rate = sampleRate;
    if (snd_pcm_hw_params_set_rate_near(dev, hw_params, &actual_sample_rate, 0) < 0) {
        perror("Error: could not set sample rate");
        snd_pcm_hw_params_free(hw_params);
        return -1;
    }
    // 요청한 샘플 레이트와 실제 설정된 샘플 레이트가 다를 경우 경고 출력
    if (actual_sample_rate != sampleRate) {
        fprintf(stderr, "Warning: Sample rate set to %u Hz (requested %d Hz)\n", actual_sample_rate, sampleRate);
    }

    // 채널 수 설정
    if (snd_pcm_hw_params_set_channels(dev, hw_params, channels) < 0) {
        perror("Error: could not set channels count");
        snd_pcm_hw_params_free(hw_params);
        return -1;
    }

    // 버퍼 조각(periods) 수 설정 (가장 가까운 값으로 설정될 수 있음)
    int dir = 0; // 0: 정확한 값, 1: 더 큰 값, -1: 더 작은 값
    if (snd_pcm_hw_params_set_periods_near(dev, hw_params, (unsigned int*)&fragments, &dir) < 0) {
        perror("Error: could not set periods (fragments)");
        snd_pcm_hw_params_free(hw_params);
        return -1;
    }
    // fprintf(stderr, "Periods set to %d\n", fragments); // 디버깅용

    // 버퍼 사이즈 설정 (프레임 단위). ALSA는 이 값을 조정할 수 있습니다.
    actual_buffer_frames = buffer_frames;
    dir = 0;
    if (snd_pcm_hw_params_set_buffer_size_near(dev, hw_params, &actual_buffer_frames) < 0) {
        perror("Error: could not set buffer size");
        snd_pcm_hw_params_free(hw_params);
        return -1;
    }
    // fprintf(stderr, "Buffer size set to %lu frames\n", actual_buffer_frames); // 디버깅용

    // 설정된 하드웨어 파라미터를 PCM 장치에 적용
    if (snd_pcm_hw_params(dev, hw_params) < 0) {
        perror("Error: could not set HW parameters");
        snd_pcm_hw_params_free(hw_params);
        return -1;
    }

    snd_pcm_hw_params_free(hw_params); // 할당된 메모리 해제
    return 0;
}