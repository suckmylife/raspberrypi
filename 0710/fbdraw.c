// #include <stdio.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <linux/fb.h>
// #include <sys/ioctl.h>

// #define FBDEVICE "/dev/fb0"

// typedef unsigned char ubyte;
// struct fb_var_screeninfo vinfo;	/* 프레임 버퍼 정보 처리를 위한 구조체 */

// unsigned short makepixel(unsigned char r, unsigned char g, unsigned char b) {
//     return (unsigned short)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
// }
// #if 1
// // static int drawpoint(int fd, int x, int y, unsigned short color)
// // {

// //     /* 색상 출력을 위한 위치 계산 : offset  = (X의_위치+Y의_위치x해상도의_넓이)x2  */
// //     int offset = (x + y*vinfo.xres)*2;
// //     lseek(fd, offset, SEEK_SET);
// //     write(fd, &color, 2);
// //     return 0;
// // }
// static void drawpoint(int fd, int x, int y, ubyte r, ubyte g, ubyte b)
// {
//     ubyte a = 0xFF;

//     /* 색상 출력을 위한 위치를 구한다. */
//     /* offset = (X의_위치 + Y의_위치 × 해상도의_넓이) × 색상의_바이트_수 */
//     int offset = (x + y*vinfo.xres)*vinfo.bits_per_pixel/8.; 
//     lseek(fd, offset, SEEK_SET);
//     write(fd, &b, 1);
//     write(fd, &g, 1);
//     write(fd, &r, 1);
//     write(fd, &a, 1);
// }
// #else
// /* 점을 그린다. */
// static void drawpoint(int fd, int x, int y, ubyte r, ubyte g, ubyte b)
// {
//     ubyte a = 0xFF;

//     /* 색상 출력을 위한 위치를 구한다. */
//     /* offset = (X의_위치 + Y의_위치 × 해상도의_넓이) × 색상의_바이트_수 */
//     int offset = (x + y*vinfo.xres)*vinfo.bits_per_pixel/8.; 
//     lseek(fd, offset, SEEK_SET);
//     write(fd, &b, 1);
//     write(fd, &g, 1);
//     write(fd, &r, 1);
//     write(fd, &a, 1);
// }
// #endif
// int main(int argc, char **argv)
// {
//     int fbfd, status, offset;

//     fbfd = open(FBDEVICE, O_RDWR); 	/* 사용할 프레임 버퍼 디바이스를 연다. */
//     if(fbfd < 0) {
//         perror("Error: cannot open framebuffer device");
//         return -1;
//     }

//     /* 현재 프레임 버퍼에 대한 화면 정보를 얻어온다. */
//     if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
//         perror("Error reading fixed information");
//         return -1;
//     }
// #if 1
//     // drawpoint(fbfd, 50, 50, makepixel(255, 0, 0));            /*  Red 점을 출력 */
//     // drawpoint(fbfd, 100, 100, makepixel(0, 255, 0));        	/*  Green 점을 출력 */
//     // drawpoint(fbfd, 150, 150, makepixel(0, 0, 255));        	/*  Blue 점을 출력 */
//     drawpoint(fbfd, 50, 50, 255, 0, 0); 		/* 빨간색(Red) 점을 출력 */
//     drawpoint(fbfd, 100, 100, 0, 255, 0); 	/* 초록색(Green) 점을 출력 */
//     drawpoint(fbfd, 150, 150, 0, 0, 255); 	/* 파란색(Blue) 점을 출력 */
// #else
//     drawpoint(fbfd, 50, 50, 255, 0, 0); 		/* 빨간색(Red) 점을 출력 */
//     drawpoint(fbfd, 100, 100, 0, 255, 0); 	/* 초록색(Green) 점을 출력 */
//     drawpoint(fbfd, 150, 150, 0, 0, 255); 	/* 파란색(Blue) 점을 출력 */
// #endif

//     close(fbfd); 		/* 사용이 끝난 프레임 버퍼 디바이스를 닫는다. */

//     return 0;
// }
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <math.h> // roundf, fabs 함수를 위해 필요

#define FBDEVICE "/dev/fb0"

typedef unsigned char ubyte;
struct fb_var_screeninfo vinfo;

unsigned short makepixel(unsigned char r, unsigned char g, unsigned char b) {
    return (unsigned short)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}

static void drawpoint(int fd, int x, int y, ubyte r, ubyte g, ubyte b)
{
    ubyte a = 0xFF;

    // 화면 경계 내에 있는지 확인
    if (x < 0 || x >= vinfo.xres || y < 0 || y >= vinfo.yres) {
        return;
    }

    int offset = (x + y * vinfo.xres) * vinfo.bits_per_pixel / 8;
    lseek(fd, offset, SEEK_SET);
    write(fd, &b, 1);
    write(fd, &g, 1);
    write(fd, &r, 1);
    write(fd, &a, 1);
}

// 간단한 DDA 선 그리기 알고리즘
static void drawline(int fd, int x0, int y0, int x1, int y1, ubyte r, ubyte g, ubyte b) {
    int dx = x1 - x0;
    int dy = y1 - y0;

    int steps;
    // x 또는 y 방향 중 더 긴 거리를 단계 수로 설정
    if (fabs(dx) > fabs(dy)) {
        steps = fabs(dx);
    } else {
        steps = fabs(dy);
    }

    // 각 단계에서 x와 y의 증분 계산
    float xIncrement = (float)dx / (float)steps;
    float yIncrement = (float)dy / (float)steps;

    float x = x0;
    float y = y0;

    // 단계 수만큼 반복하며 점 그리기
    for (int i = 0; i <= steps; i++) {
        drawpoint(fd, roundf(x), roundf(y), r, g, b); // 부동 소수점을 반올림하여 픽셀 좌표 얻기
        x += xIncrement;
        y += yIncrement;
    }
}

static void drawcircle(int fd, int center_x, int center_y, int radius, ubyte r,ubyte g, ubyte b){
    int x = radius, y = 0;
    int radiusError = 1 - x;

    while(x >= y){
        drawpoint(fd, x+center_x, y + center_y, r, g, b);
        drawpoint(fd, y+center_x, x + center_y, r, g, b);
        drawpoint(fd, -x+center_x, y + center_y, r, g, b);
        drawpoint(fd, -y+center_x, x + center_y, r, g, b);
        drawpoint(fd, -x+center_x, -y + center_y, r, g, b);
        drawpoint(fd, -y+center_x, -x + center_y, r, g, b);
        drawpoint(fd, x+center_x, -y + center_y, r, g, b);
        drawpoint(fd, y+center_x, -x + center_y, r, g, b);

        y++;
        if(radiusError < 0){
            radiusError += 2 * y + 1;
        }else{
            x--;
            radiusError += 2 * (y - x + 1);
        }
    }
}

int main(int argc, char **argv)
{
    int fbfd;

    fbfd = open(FBDEVICE, O_RDWR);
    if(fbfd < 0) {
        perror("Error: cannot open framebuffer device");
        return -1;
    }

    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("Error reading fixed information");
        return -1;
    }

    // (100,200)에서 (300, 150)까지 초록색 선 그리기
    drawline(fbfd, 100, 200, 300, 150, 0, 255, 0);
    //원그리기
    drawcircle(fbfd, 200,200,100,255,0,255);

    close(fbfd);

    return 0;
}