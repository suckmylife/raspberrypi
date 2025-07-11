// #include <stdio.h>
// #include "bmpHeader.h"

// int readBmp(char *filename, unsigned char **data, int *cols, int *rows){
//     BITMAPFILEHEADER bmpHeader;
//     BITMAPINFOHEADER bmpInfoHeader;
//     FILE *fp;

//     fp = fopen(filename,"rb");
//     if(fp == NULL){
//         perror("ERROR\n");
//         return -1;
//     }

//     fread(&bmpHeader,sizeof(BITMAPFILEHEADER),1,fp);

//     fread(&bmpInfoHeader, sizeof(BITMAPINFOHEADER),1,fp);

//     if(bmpInfoHeader.biBitCount != 24){
//         perror("this image file doesn't supports 24bit color");
//         fclose(fp);
//         return -1;
//     }

//     *cols = bmpInfoHeader.biWidth;
//     *rows = bmpInfoHeader.biHeight;

//     printf("Rsolution : %d x %d \n",bmpInfoHeader.biWidth,bmpInfoHeader.biHeight);
//     printf("Bit Count : %d \n",bmpInfoHeader.biBitCount);

//     fseek(fp,bmpHeader.bfSize-bmpHeader.bfOffBits, fp);
//     fclose(fp);
//     return 0;
// }

#include <stdio.h>   // fopen, fread, fclose, perror, fprintf
#include <stdlib.h>  // malloc, free
#include <string.h>  // memset (필요 시)

// 비트맵 파일 헤더와 정보 헤더 구조체 정의
// 중요: 컴파일러에 따라 구조체 멤버 간 패딩이 발생할 수 있으므로,
// #pragma pack(push, 1)을 사용하여 1바이트 정렬을 강제해야 합니다.
// 이는 BMP 파일의 헤더 구조가 고정된 바이트 오프셋을 가지기 때문입니다.
#pragma pack(push, 1)

typedef struct tagBITMAPFILEHEADER {
    unsigned short bfType;      // 파일 타입 (BM)
    unsigned int   bfSize;      // 파일 전체 크기
    unsigned short bfReserved1; // 예약된 공간 (0)
    unsigned short bfReserved2; // 예약된 공간 (0)
    unsigned int   bfOffBits;   // 비트맵 데이터 시작 오프셋
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER {
    unsigned int   biSize;          // 현재 구조체 크기 (40)
    int            biWidth;         // 비트맵 너비 (픽셀)
    int            biHeight;        // 비트맵 높이 (픽셀)
    unsigned short biPlanes;        // 플레인 수 (1)
    unsigned short biBitCount;      // 픽셀당 비트 수 (1, 4, 8, 16, 24, 32)
    unsigned int   biCompression;   // 압축 방식
    unsigned int   biSizeImage;     // 이미지 데이터 크기 (바이트)
    int            biXpelsPerMeter; // 가로 해상도 (미터당 픽셀)
    int            biYpelsPerMeter; // 세로 해상도 (미터당 픽셀)
    unsigned int   biClrUsed;       // 사용된 색상 수
    unsigned int   biClrImportant;  // 중요한 색상 수
} BITMAPINFOHEADER;

#pragma pack(pop) // 1바이트 정렬 해제


// BMP 파일을 읽고 픽셀 데이터를 반환하는 함수
// filename: 읽을 BMP 파일 경로
// data: 읽어온 픽셀 데이터가 저장될 포인터의 주소 (함수 내에서 malloc으로 할당됨)
// cols: 이미지 너비(열 수)가 저장될 변수의 주소
// rows: 이미지 높이(행 수)가 저장될 변수의 주소
int readBmp(char *filename, unsigned char **data, int *cols, int *rows) {
    BITMAPFILEHEADER bmpHeader;
    BITMAPINFOHEADER bmpInfoHeader;
    FILE *fp;

    fp = fopen(filename, "rb"); // 이진 읽기 모드로 파일 열기
    if (fp == NULL) {
        perror("Error opening BMP file"); // 더 구체적인 에러 메시지
        return -1;
    }

    // BITMAPFILEHEADER 읽기
    if (fread(&bmpHeader, sizeof(BITMAPFILEHEADER), 1, fp) != 1) {
        fprintf(stderr, "Error reading BITMAPFILEHEADER.\n");
        fclose(fp);
        return -1;
    }

    // BMP 파일 시그니처 ('BM' 또는 0x4D42) 확인
    if (bmpHeader.bfType != 0x4D42) { // 'BM' 아스키 값
        fprintf(stderr, "Error: Not a valid BMP file (incorrect bfType).\n");
        fclose(fp);
        return -1;
    }

    // BITMAPINFOHEADER 읽기
    if (fread(&bmpInfoHeader, sizeof(BITMAPINFOHEADER), 1, fp) != 1) {
        fprintf(stderr, "Error reading BITMAPINFOHEADER.\n");
        fclose(fp);
        return -1;
    }

    // 24비트 BMP만 지원하도록 검사 (필요에 따라 다른 비트 심도 지원 로직 추가)
    if (bmpInfoHeader.biBitCount != 24) {
        fprintf(stderr, "Error: This BMP file uses %d-bit color, only 24-bit is supported.\n", bmpInfoHeader.biBitCount);
        fclose(fp);
        return -1;
    }

    // 압축되지 않은 BMP 파일만 지원 (BI_RGB = 0)
    if (bmpInfoHeader.biCompression != 0) {
        fprintf(stderr, "Error: Compressed BMP files are not supported.\n");
        fclose(fp);
        return -1;
    }

    // 이미지 너비와 높이 설정
    *cols = bmpInfoHeader.biWidth;
    *rows = bmpInfoHeader.biHeight;

    printf("Resolution : %d x %d \n", *cols, *rows);
    printf("Bit Count : %d \n", bmpInfoHeader.biBitCount);
    printf("Image data starts at offset: %u\n", bmpHeader.bfOffBits);


    // --- 파일 포인터를 픽셀 데이터 시작 위치로 이동 ---
    // BMP 픽셀 데이터는 bfOffBits만큼 떨어진 곳에서 시작합니다.
    if (fseek(fp, bmpHeader.bfOffBits, SEEK_SET) != 0) {
        perror("Error: fseek failed to move to pixel data start");
        fclose(fp);
        return -1;
    }

    // --- 픽셀 데이터 크기 계산 및 메모리 할당 ---
    // 24비트 BMP는 픽셀당 3바이트를 사용합니다.
    int bytes_per_pixel = bmpInfoHeader.biBitCount / 8; // 항상 3 (24/8)

    // BMP 파일의 각 스캔라인(가로 한 줄)은 4의 배수가 되도록 패딩됩니다.
    // 따라서 실제 한 줄의 바이트 수를 계산해야 합니다.
    int padded_row_size = ((bmpInfoHeader.biWidth * bytes_per_pixel + 3) / 4) * 4;

    // 전체 픽셀 데이터의 크기 (패딩 포함)
    size_t total_data_size = (size_t)padded_row_size * bmpInfoHeader.biHeight;

    // 픽셀 데이터를 저장할 메모리 할당
    *data = (unsigned char *)malloc(total_data_size);
    if (*data == NULL) {
        perror("Error: malloc for BMP data failed");
        fclose(fp);
        return -1;
    }

    // --- 파일에서 픽셀 데이터 읽기 ---
    // fread 함수는 총 읽은 바이트 수를 반환합니다.
    if (fread(*data, 1, total_data_size, fp) != total_data_size) {
        fprintf(stderr, "Error reading BMP pixel data. Read %zu bytes, expected %zu.\n",
                fread(*data, 1, total_data_size, fp), total_data_size);
        free(*data); // 할당된 메모리 해제
        *data = NULL; // NULL로 설정하여 외부에서 잘못 사용 방지
        fclose(fp);
        return -1;
    }

    // 파일 닫기
    fclose(fp);

    return 0; // 성공적으로 데이터를 읽음
}