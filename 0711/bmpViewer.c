// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <fcntl.h>
// #include <limits.h>
// #include <unistd.h>
// #include <sys/mman.h>
// #include <sys/ioctl.h>
// #include <linux/fb.h>

// #include "bmpHeader.h"

// #define FBDEVFILE "/dev/fb0"
// #define LIMIT_UBYTE(n) (n>UCHAR_MAX)?UCHAR_MAX:(n<0)?0:n

// typedef unsigned char ubyte;

// extern int readBmp(char *filename, ubyte **pData, int *cols, int *rows, int *color);

// int main(int argc, char **argv)
// {
//     int cols, rows, color = 24;
//     ubyte r, g, b, a = 255;
//     ubyte *pData, *pBmpData;
//     //*pFbMap;
//     unsigned short *pFbMap16;
//     struct fb_var_screeninfo vinfo;
//     int fbfd;

//     if(argc != 2){
//         printf("Usage : ./%s xxx.bmp\n",argv[0]);
//         return 0;
//     }

//     fbfd = open(FBDEVFILE, O_RDWR);
//     if(fbfd < 0){
//         perror("Open() Not");
//         return -1;
//     }

//     if(ioctl(fbfd,FBIOGET_VSCREENINFO, &vinfo) < 0) {
//         perror("ioctl() FBIOGET_VSCREENINFO");
//         return -1;
//     }

//     //pBmpData = (ubyte *)malloc(vinfo.xres * vinfo.yres * sizeof(ubyte) * vinfo.bits_per_pixel/8);
//     //pData    = (ubyte *)malloc(vinfo.xres * vinfo.yres * sizeof(ubyte) * color /8);
//     //pFbMap   = (ubyte *)mmap(0,vinfo.xres * vinfo.yres * vinfo.bits_per_pixel/8, PROT_READ | PROT_WRITE,MAP_SHARED,fbfd,0);
//     pBmpData = (ubyte *)malloc(vinfo.xres * vinfo.yres * (vinfo.bits_per_pixel / 8));
//     if (pBmpData == NULL) {
//         perror("malloc pBmpData");
//         close(fbfd);
//         return -1;
//     }

//     pData = (ubyte *)malloc(vinfo.xres * vinfo.yres * (color / 8));
//     if (pData == NULL) {
//         perror("malloc pData");
//         free(pBmpData);
//         close(fbfd);
//         return -1;
//     }

//     pFbMap16 = (unsigned short *)mmap(0, vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8,
//                                     PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

//     if ((unsigned long)pFbMap16 == (unsigned long)-1) {
//         perror("mmap()");
//         free(pBmpData);
//         free(pData);
//         close(fbfd);
//         return -1;
//     }

//     // if((unsigned)pFbMap == (unsigned)-1){
//     //     perror("mmap()");
//     //     return -1;
//     // }

//     if(readBmp(argv[1],&pData,&cols,&rows,&color) < 0){
//         perror("readBmp()");
//         munmap(pFbMap16, vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);
//         free(pBmpData);
//         free(pData);
//         close(fbfd);
//         return -1;
//     }
//     int bmp_bytes_per_pixel = color / 8; // BMP 원본 픽셀당 바이트 수
//     for (int y = 0; y < rows; y++) {
//         // BMP 파일은 보통 하단부터 상단으로 픽셀 데이터가 저장됩니다.
//         // 따라서 y축을 반전하여 읽습니다.
//         int k = (rows - 1 - y) * cols * bmp_bytes_per_pixel;

//         // 프레임버퍼에 쓸 위치를 계산합니다. (y * 한 줄의 바이트 수)
//         int fb_line_offset_bytes = y * vinfo.xres * (vinfo.bits_per_pixel / 8);
        
//         for (int x = 0; x < cols; x++) {
//             // BMP 데이터 (pData)에서 B, G, R 값을 읽습니다.
//             // BMP는 일반적으로 BGR 순서입니다.
//             b = LIMIT_UBYTE(pData[k + x * bmp_bytes_per_pixel + 0]); // Blue
//             g = LIMIT_UBYTE(pData[k + x * bmp_bytes_per_pixel + 1]); // Green
//             r = LIMIT_UBYTE(pData[k + x * bmp_bytes_per_pixel + 2]); // Red

//             // 16비트 RGB 565 형식으로 변환합니다: RRRRRGGGGGGBBBBB
//             // R (5비트): r >> 3
//             // G (6비트): g >> 2
//             // B (5비트): b >> 3
//             unsigned short pixel16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

//             // pBmpData 대신 pFbMap16 (프레임버퍼에 직접 매핑된 메모리)에 씁니다.
//             // pBmpData는 중간 버퍼 역할을 하며, 실제 화면에 그리기 위해서는 pFbMap16에 써야 합니다.
//             // 만약 pBmpData에만 쓰고 pFbMap으로 복사하는 로직이 있다면 해당 로직을 살려야 합니다.
//             // 여기서는 pFbMap16에 직접 쓰는 것으로 가정합니다.

//             // 프레임버퍼의 x, y 위치에 16비트 픽셀 값 쓰기
//             *(pFbMap16 + (fb_line_offset_bytes / 2) + x) = pixel16;
//             // fb_line_offset_bytes / 2 는 바이트 오프셋을 unsigned short (2바이트) 오프셋으로 변환
//         }
//     }
//     munmap(pFbMap16, vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);
//     free(pBmpData);
//     free(pData);
//     close(fbfd);

//     return 0;
// }

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <string.h> // memcpy를 위해 추가
#include <limits.h> // UCHAR_MAX, USHRT_MAX를 위해 추가

// bmpHeader.c에 정의된 BITMAPFILEHEADER와 BITMAPINFOHEADER 구조체가 필요합니다.
// 여기서는 편의상 포함하지 않지만, 실제 컴파일 시에는 해당 헤더 파일이나 정의가 필요합니다.
// 예를 들어:
// #include "bmpHeader.h" // 또는 직접 정의

typedef unsigned char ubyte; // ubyte 정의

#define FBDEVFILE            "/dev/fb0"
#define BMP_BPP              24 // BMP 파일의 비트 심도를 명확히 24비트로 정의

// LIMIT_USHRT, LIMIT_UBYTE 매크로는 n이 unsigned type이므로 음수 체크는 불필요하지만,
// 안전을 위해 그대로 둡니다. 하지만 보통은 UCHAR_MAX/USHRT_MAX 초과 여부만 확인합니다.
#define LIMIT_USHRT(n) ((n > USHRT_MAX) ? USHRT_MAX : (unsigned short)n) // unsigned short 범위
#define LIMIT_UBYTE(n) ((n > UCHAR_MAX) ? UCHAR_MAX : (unsigned char)n) // unsigned char 범위

// readBmp 함수는 이제 int *color 인자를 받지 않는 형태로 외부 선언됩니다.
// 이는 제공된 bmpHeader.c의 readBmp 함수와 일치합니다.
extern int readBmp(char *filename, unsigned char **pData, int *cols, int *rows);

// 24비트 RGB (888)에서 16비트 RGB 565로 변환하는 함수
unsigned short makepixel(unsigned char r, unsigned char g, unsigned char b) {
    // R (5비트): r >> 3
    // G (6비트): g >> 2
    // B (5비트): b >> 3
    return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

int main(int argc, char **argv)
{
    int bmp_width, bmp_height; // BMP 파일의 실제 너비와 높이
    int fb_width, fb_height;   // 프레임버퍼의 너비와 높이
    unsigned char *pData;      // BMP 원본 24비트 데이터 (BGR 순서)
    unsigned char r, g, b;
    unsigned short *pBmpData;  // 16비트로 변환된 BMP 데이터를 저장할 버퍼
    unsigned short *pfbmap;    // 프레임버퍼에 매핑된 메모리 포인터
    unsigned short pixel;      // 변환된 16비트 픽셀 값
    struct fb_var_screeninfo vinfo;
    int fbfd;
    int x, y, k, t;

    if(argc != 2) {
        printf("\nUsage: ./%s xxx.bmp\n", argv[0]);
        return 0;
    }

    /* 프레임버퍼를 오픈한다. */
    fbfd = open(FBDEVFILE, O_RDWR);
    if(fbfd < 0) {
        perror("open( )");
        return -1;
    }

    /* 현재 프레임버퍼에 대한 고정된 화면 정보를 얻어온다. */
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("ioctl( ) : FBIOGET_VSCREENINFO");
        close(fbfd); // 에러 시 fbfd 닫기
        return -1;
    }

    // 프레임버퍼의 실제 너비와 높이를 가져옵니다.
    fb_width = vinfo.xres;
    fb_height = vinfo.yres;

    // 프레임버퍼의 픽셀당 비트 수를 확인합니다. 16비트가 아니면 경고 또는 종료.
    if (vinfo.bits_per_pixel != 16) {
        fprintf(stderr, "Error: Framebuffer is not 16 bits per pixel (%d bpp detected).\n", vinfo.bits_per_pixel);
        close(fbfd);
        return -1;
    }

    /* BMP 출력을 위한 변수의 메모리 할당 */
    // pBmpData는 16비트 픽셀을 저장하므로 unsigned short 타입 크기만큼 할당합니다.
    // 여기서는 화면 전체 크기(fb_width * fb_height)만큼 할당합니다.
    pBmpData = (unsigned short *)malloc(fb_width * fb_height * sizeof(unsigned short));
    if (pBmpData == NULL) {
        perror("malloc pBmpData");
        close(fbfd);
        return -1;
    }

    // pData는 readBmp 함수에서 읽어올 BMP 원본 데이터를 저장할 버퍼입니다.
    // readBmp가 24비트 BMP를 읽는다고 가정하고, BMP의 예상 최대 해상도를 고려하여 할당해야 합니다.
    // 여기서는 임시로 프레임버퍼 해상도를 기준으로 NO_OF_COLOR(24/8=3) 바이트를 곱해 할당합니다.
    // 실제 readBmp 함수에서 *pData에 메모리를 할당한다면, 이 할당은 불필요할 수 있습니다.
    // (이전 readBmp 분석 시 *pData에 malloc이 없었으므로 여기에서 하는 것이 맞습니다.)
    pData = (unsigned char *)malloc(fb_width * fb_height * (BMP_BPP / 8)); // 24/8 = 3 바이트
    if (pData == NULL) {
        perror("malloc pData");
        free(pBmpData); // pBmpData도 해제
        close(fbfd);
        return -1;
    }

    /* 프레임버퍼에 대한 메모리 맵을 수행한다. */
    // pfbmap은 16비트 포인터로 캐스팅하고, 크기는 fb_width * fb_height * 2 바이트 (16비트 = 2바이트)
    pfbmap = (unsigned short *)mmap(0, fb_width * fb_height * (vinfo.bits_per_pixel / 8),
                                    PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if((unsigned long)pfbmap == (unsigned long)-1) { // 64비트 시스템 고려하여 unsigned long으로 캐스팅
        perror("mmap( )");
        free(pBmpData);
        free(pData);
        close(fbfd);
        return -1;
    }

    /* BMP 파일에서 헤더 정보를 가져오고 픽셀 데이터를 pData에 읽어온다. */
    // readBmp 함수는 pData에 메모리를 할당하고 픽셀 데이터를 채워야 합니다.
    // (이전 bmpHeader.c 분석 결과, 이 함수에 픽셀 데이터 읽기 로직이 없었습니다.
    // 따라서 readBmp 함수를 수정해야 합니다. 여기서는 수정되었다고 가정합니다.)
    if(readBmp(argv[1], &pData, &bmp_width, &bmp_height) < 0) {
        perror("readBMP( )");
        // mmap, malloc 해제
        munmap(pfbmap, fb_width * fb_height * (vinfo.bits_per_pixel / 8));
        free(pBmpData);
        free(pData);
        close(fbfd);
        return -1;
    }

    printf("\nBMP Width : %d, BMP Height : %d\n", bmp_width, bmp_height);
    printf("Framebuffer Resolution : %d x %d\n", fb_width, fb_height);

    /* 24 비트의 BMP 이미지 데이터를 16비트 이미지 데이터로 변경 */
    // 이중 for 루프는 BMP 파일의 해상도(width, height)만큼 반복합니다.
    // 변환된 데이터는 pBmpData 버퍼에 저장됩니다.
    for(y = 0; y < bmp_height; y++) {
        // BMP 파일은 일반적으로 하단부터 상단으로 픽셀 데이터가 저장됩니다.
        // 따라서 y축을 반전하여 읽습니다.
        k = (bmp_height - 1 - y) * bmp_width * (BMP_BPP / 8); // BMP 원본 데이터의 오프셋 (바이트)
        
        // pBmpData에 쓸 현재 줄의 시작 오프셋 (unsigned short 단위, 즉 픽셀 단위)
        // 화면 해상도와 BMP 해상도가 다를 수 있으므로 여기서는 pBmpData에 저장할 위치를
        // BMP 해상도를 기준으로 계산하고, 나중에 memcpy할 때 화면 해상도를 사용합니다.
        // 단, BMP 해상도와 프레임버퍼 해상도가 다르면 잘리거나 늘어날 수 있습니다.
        // 현재 로직은 BMP 데이터를 pBmpData에 BMP 해상도 그대로 채우는 방식입니다.
        t = y * bmp_width; // unsigned short 단위 (픽셀 단위) 오프셋
        
        for(x = 0; x < bmp_width; x++) {
            // BMP 데이터 (pData)에서 B, G, R 값을 읽습니다. (BMP는 보통 BGR 순서)
            b = LIMIT_UBYTE(pData[k + x * (BMP_BPP / 8) + 0]); // Blue
            g = LIMIT_UBYTE(pData[k + x * (BMP_BPP / 8) + 1]); // Green
            r = LIMIT_UBYTE(pData[k + x * (BMP_BPP / 8) + 2]); // Red

            pixel = LIMIT_USHRT(makepixel(r, g, b));
            pBmpData[t + x] = pixel; // 16비트 픽셀을 pBmpData에 저장
        };
    };

    /* 앞에서 생성한 16비트 BMP 데이터 메모리맵된 메모리 공간으로 복사 */
    // 복사할 데이터의 크기는 프레임버퍼 해상도(fb_width, fb_height)를 기준으로 합니다.
    // 만약 BMP 해상도(bmp_width, bmp_height)가 프레임버퍼 해상도보다 작으면 일부만 채워지고,
    // 크면 잘려서 복사됩니다.
    memcpy(pfbmap, pBmpData, fb_width * fb_height * (vinfo.bits_per_pixel / 8)); // vinfo.bits_per_pixel/8 = 2

    /* 사용한 메모리맵 공간과 설정된 메모리를 해제하고 프레임버퍼 파일을 닫음 */
    munmap(pfbmap, fb_width * fb_height * (vinfo.bits_per_pixel / 8));
    free(pBmpData);
    free(pData);

    close(fbfd);

    return 0;
}