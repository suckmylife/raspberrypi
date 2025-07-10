#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <math.h> // roundf, fabs 함수를 위해 필요
#include <sys/mman.h>
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
    int offset = (x + y * vinfo.xres) * 2;
    unsigned short pixel_color = makepixel(r, g, b);
    lseek(fd, offset, SEEK_SET);
    write(fd, &pixel_color, 2);
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

static void drawface(int fd, int start_x,int start_y,int end_x,int end_y,ubyte r,ubyte g, ubyte b){
    ubyte a = 0xFF;
    if(end_x == 0) end_x = vinfo.xres;
    if(end_y == 0) end_y = vinfo.yres;
    unsigned short pixel_color = makepixel(r, g, b);
    for(int x = start_x; x < end_x; x++){
        for(int y = start_y; y<end_y; y++){
            int offset =  (x + y * vinfo.xres) * 2;
            lseek(fd, offset, SEEK_SET);
            write(fd, &pixel_color, 2);
        }
    }
}

static void drawfacemmap(int fd, int start_x,int start_y,int end_x,int end_y,ubyte r,ubyte g, ubyte b){
    //ubyte *pfb, a = 0xFF;
    int color = vinfo.bits_per_pixel/8;
    if(end_x == 0) end_x = vinfo.xres;
    if(end_y == 0) end_y = vinfo.yres;
    unsigned short *pfb = (unsigned short *)mmap(0,vinfo.xres * vinfo.yres * color, PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
    // 16비트 픽셀 색상 생성
    unsigned short pixel_color = makepixel(r, g, b);
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            // (x, y) 좌표에 해당하는 16비트 픽셀 포인터 계산
            // pfb는 unsigned short 포인터이므로, vinfo.xres만큼 더하면 다음 행의 같은 x 위치로 이동
            *(pfb + x + y * vinfo.xres) = pixel_color;
        }
    }

    // 매핑된 메모리 해제
    munmap(pfb, vinfo.xres * vinfo.yres * color);
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
    //프랑스 국기
    // drawface(fbfd, 0,0,0,0,255,255,255);
    // drawface(fbfd, 0,0,324,800,0,0,255);
    // drawface(fbfd, 700,0,1024,800,255,0,0);
    // //티원 로고part1
    drawfacemmap(fbfd, 0,0,0,0,255,255,255);
    drawline(fbfd, 200,100,600,100,255,0,0);
    drawline(fbfd, 600,100,400,400,255,0,0);
    drawline(fbfd, 400,400,370,360,255,0,0);
    drawline(fbfd, 370,360,520,140,255,0,0);
    drawline(fbfd, 520,140,220,140,255,0,0);
    drawline(fbfd, 220,140,200,100,255,0,0);
    // //drawface(fbfd, 0,100,100,300,0,0,255);
    // //part2
    drawline(fbfd, 250,165,470,165,255,0,0);
    drawline(fbfd, 470,165,450,195,255,0,0);
    drawline(fbfd, 450,195,270,195,255,0,0);
    drawline(fbfd, 270,195,250,165,255,0,0);
    // //part3
    drawline(fbfd, 290,220,440,220,255,0,0);
    drawline(fbfd, 440,220,420,250,255,0,0);
    drawline(fbfd, 420,250,310,250,255,0,0);
    drawline(fbfd, 310,250,290,220,255,0,0);
    // //part4
    drawline(fbfd, 330,275,410,275,255,0,0);
    drawline(fbfd, 410,275,390,305,255,0,0);
    drawline(fbfd, 390,305,350,305,255,0,0);
    drawline(fbfd, 350,305,330,275,255,0,0);
    //part5
    drawline(fbfd, 625,100,600,140,255,0,0);
    drawline(fbfd, 600,140,625,140,255,0,0);
    drawline(fbfd, 625,140,460,400,255,0,0);
    //drawline(fbfd, 400,380,380,400,255,0,0);
    // (100,200)에서 (300, 150)까지 초록색 선 그리기
    //drawline(fbfd, 100, 200, 300, 150, 0, 255, 0);
    //원그리기
    //drawcircle(fbfd, 200,200,100,255,0,255);
    //수정된 mmap
    //drawfacemmap(fbfd,0,0,0,0,255,255,0);

    close(fbfd);

    return 0;
}
