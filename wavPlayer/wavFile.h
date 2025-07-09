/**********************************************************
 *    wavheader.h
**********************************************************/

#ifndef __WAVHEADER_H
#define __WAVHEADER_H

//    block_align = channels * bits / 8;
//    bps = sample_rate * channels * bits / 8;
//    size = data_size + sizeof(wavheader_t) - 8;
typedef struct wavFile_t {
  unsigned char  riffID[4];        /* "RIFF" */ 
				   /* riffID[] = {'R', 'I', 'F', 'F'}; */
  unsigned long  riffLen;          /* Size of data to follow */
  unsigned char  waveID[4];         /* "WAVE" */
  				   /* waveID[] = {'W', 'A', 'V', 'E'}; */
  unsigned char  fmtID[4];         /* "fmt " */
  				   /* fmtID[] = {'f', 'm', 't', ' '}; */
  unsigned long  fmtLen;           /* 16 */
  unsigned short fmtTag;           /* 1 */
  unsigned short nChannels;        /* Number of Channels */
  unsigned long  sampleRate;       /* Sample Rate */
  unsigned long  avgBytesPerSec;   /* Bytes / sec */
  unsigned short nblockAlign;      /* 1 */
  unsigned short bitsPerSample;    /* Bits / sample */
  unsigned char  dataID[4];        /* "data" */
  				   /* dataID[] = {'d', 'a', 't', 'a'}; */
  unsigned long  dataLen;          /* Number of Samples */
} wavHeader;

#endif
