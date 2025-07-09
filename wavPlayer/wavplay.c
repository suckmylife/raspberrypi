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

#define	BUF_SIZE	1024
#define	AUDIO_DEV	"/dev/dsp"

int main(int argc, char** argv)
{
	int fd = -1, fd_sd = -1;
	long count;
	unsigned char data[BUF_SIZE];
	int channels;
	int format;
	wavHeader wavheader;

	// Initialize Header of Wave
	memset(&wavheader, 0, sizeof(wavHeader));

	if(argc <= 1) {
		printf("usage : %s filename\n", argv[0]);
		exit(-1);
	} else {
		printf("Playing file : %s\n", argv[1]);
	}

	if((fd = open(argv[1], O_RDONLY)) == -1) {
		printf("Could not open the specified wave file : %s\n", argv[1]);
		exit(-1);
	}

	// Read Head of wave file
	if((count = read(fd, &wavheader, sizeof(wavHeader))) < 1) {
		printf("Could not read wave data\n");
		exit(-1);
	}

	if((fd_sd = open(AUDIO_DEV, O_WRONLY)) == -1) {
		printf("Error : Cound not open the audio device\n");
		exit(-1);
	}
	
	channels = wavheader.nChannels - 1;
	printf("Wave Channel Mode : %s\n", (channels)? "Stereo":"Mono");

	if((ioctl(fd_sd, /* fill the blank */, &channels)) == -1) {
		printf("Error : Cound not set Channels\n");
		exit(-1);
	} else {
		printf("Channel set to %s\n", (channels)? "Stereo":"Mono");
	}

	printf("Wave Bytes : %d\n", wavheader.nblockAlign);
	switch(wavheader.nblockAlign) {
		case 1:
			format = AFMT_U8;
			break;
		case 2:
			format = (!channels)?AFMT_S16_LE:AFMT_U8;
			break;
		case 4:
			printf("Stereo Wave file\n");
			format = AFMT_S16_LE;
			break;
		default:
			printf("Unknown byte rate for sound\n");
			break;
	};
	
	if((ioctl(fd_sd, /* fill the blank */, &format)) == -1) {
		printf("Error : Cound not set format\n");
		exit(-1);
	} else {
		printf("Format set to %d\n", format);
	}

	printf("Wave Sampling Rate : 0x%d\n", wavheader.sampleRate);
	if((ioctl(fd_sd, /* fill the blank */, &wavheader.sampleRate)) == -1) {
		close(fd_sd);
		exit(-1);
	} else {
		printf("Speed rate set to %d\n", wavheader.sampleRate);
	}

	do {
		memset(data, 0, BUF_SIZE);
		if((count = read(fd, data, BUF_SIZE)) <= 0) break;
		if(write(fd_sd, data, count) == -1) {
			printf("Error : Could not write to Sound Device\n");
			exit(-1);
		} else {
			printf("Play Sound : %d bytes\n", count);
		}
	} while(count == BUF_SIZE);

end:
	close(fd);
	close(fd_sd);

	return 0;
}
