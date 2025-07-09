#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/soundcard.h>

#include "wavFile.h"
WAVHEADER wavHeader;
#define	BUF_SIZE	1024
#define	AUDIO_DEV	"/dev/dsp"
#define ALSA_PCM_NEW_HW_PARAMS_API

int main(int argc, char** argv)
{
	int fd = -1;
	int rc, buf_size, dir;
	long count, remain;
	unsigned int val;
	char *buffer;
	int channels;
	int format;

	snd_pcm_t *handle;
	snd_pcm_hw_params_t *params;
	snd_pcm_uframes_t frames;

	if(argc <= 1) {
		printf("usage : %s filename\n", argv[0]);
		return -1;
	} else {
		printf("Playing file : %s\n", argv[1]);
	}

	if((fd = open(argv[1], O_RDONLY)) == -1) {
		printf("Could not open the specified wave file : %s\n", argv[1]);
		return -1;
	}

	// Read Head of wave file
	if((count = read(fd, &wavheader, sizeof(WAVHEADER))) < 1) {
		printf("Could not read wave data\n");
		goto END;
	}
	rc = snd_pcm_open(&handle,"default",SND_PCM_STREAM_PLAYBACK,0);
	if(rc < 0) {
		fprintf(stderr,"unable to open pcm device : %s\n",snd_strerror(rc));
		return -1;
	}
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_any(handle,params);
	channels = wavheader.nChannels;
	printf("Wave Channel Mode : %s\n", (channels)? "Stereo":"Mono");
	snd_pcm_hw_params_set_channels(handle,params,channels);

	printf("Wave Bytes : %d\n",wavHeader.nblockAlign);
	switch (wavHeader.nblockAlign)
	{
	case 1:
		format = SND_PCM_FORMAT_U8;
		break;
	case 2:
		format = (channels==1) ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_U8;
		break;
	case 4:
		format = SND_PCM_FORMAT_S16_LE;
		break;
	default:
		printf("Unknown byte rate for sound\n");
		return -1;
	}

	snd_pcm_hw_params_set_access(handle,params,SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(handle,params,format);

	printf("Wave Sampling Rate : 0x%d\n",wavHeader.sampleRate);
	val = wavHeader.sampleRate;
	snd_pcm_hw_params_set_rate_near(handle,params,&val,&dir);

	frames = 32;
	snd_pcm_hw_params_set_period_size_near(handle,params,&frames,&dir);

	rc = snd_pcm_hw_params(handle,params);
	if(rc < 0){
		fprintf(strerror, "Unable to set hw parameters : %s\n",snd_strerror(rc));
		goto END;
	}

	snd_pcm_hw_params_get_period_size(params,&frames,&dir);
	buf_size = frames * channels * ((format == SND_PCM_FORMAT_S16_LE)?2:1);
	buffer = (char*)malloc(buf_size);

	snd_pcm_hw_params_get_period_time(params,&val,&dir);
	remain = wavHeader.dataLen;

	do {
		buf_size = (remain > buf_size)?buf_size:remain;
		if((count = read(fd, buffer, buf_size)) <= 0) break;
		remain -= count;
		rc = snd_pcm_writei(handle,buffer,frames);
		if(rc == -EPIPE) {
			printf("Error : Could not write to Sound Device\n");
			snd_pcm_prepare(handle);
		} else if(rc <0){
			fprintf(stderr,"error from write : %s\n",snd_strerror(rc));
		}else{
			fprintf(stderr,"short write, write : %d frames\n",rc);
		}
	} while(count == buf_size);

END:
	close(fd);
	sleep(1);
	snd_pcm_drain(handle);
	snd_pcm_close(handle);
	free(buffer);

	return 0;
}
