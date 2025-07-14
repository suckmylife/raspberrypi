#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/videodev2.h>

#define TCP_PORT 5100


/* 비디오 */
#define VIDEO_DEVICE        "/dev/video0"
#define FRAMEBUFFER_DEVICE  "/dev/fb0"
#define WIDTH               640
#define HEIGHT              480

static struct fb_var_screeninfo vinfo;
/* 비디오 */

int main(int argc, char **argv)
{
    int ssock;
    struct sockaddr_in servaddr, cliaddr;
    char mesg[BUFSIZ];

    if(argc < 2){
        printf("Usage : %s IP_ADDRESS \n",argv[0]);
        return -1;
    }

    if((ssock = socket(AF_INET,SOCK_STREAM, 0))<0){
        perror("socket()");
        return -1;
    }

    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;

    inet_pton(AF_INET, argv[1],&(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(TCP_PORT);

    if(connect(ssock,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0){
        perror("connect()");
        return -1;
    }

    fgets(mesg,BUFSIZ,stdin);
    ////////////////////////////////////////////////////
    int fd = open(VIDEO_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("Failed to open video device");
        return 1;
    }

    struct v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Failed to set format");
        close(fd);
        return 1;
    }

    char *buffer = malloc(fmt.fmt.pix.sizeimage);
    if (!buffer) {
        perror("Failed to allocate buffer");
        close(fd);
        return 1;
    }

    int fb_fd = open(FRAMEBUFFER_DEVICE, O_RDWR);
    if (fb_fd == -1) {
        perror("Error opening framebuffer device");
        exit(1);
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information");
        close(fb_fd);
        exit(1);
    }

    uint32_t fb_width = vinfo.xres;
    uint32_t fb_height = vinfo.yres;
    uint32_t screensize = fb_width * fb_height * vinfo.bits_per_pixel / 8;
    uint16_t *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE,                                         MAP_SHARED, fb_fd, 0);
    if ((intptr_t)fbp == -1) {
        perror("Error mapping framebuffer device to memory");
        close(fb_fd);
        exit(1);
    }
    
    while (1) {
        int totalsize = read(fd, buffer, fmt.fmt.pix.sizeimage);//클라이언트에서 하고
        if (totalsize == -1) {
            perror("Failed to read frame");
            break;
        }
        printf("totalsize : %d\n",totalsize);
        //총 사이즈 보내기 
        if(send(ssock,&totalsize,sizeof(totalsize),0) <= 0){
                perror("send()");
                return -1;
            }
        int server_read;
        if(recv(ssock,&server_read,sizeof(server_read),0)<=0){
            perror("recv");
            return -1;
        }
        //버퍼 전송
        int sent = 0;
        while(sent < totalsize){
            int chunk_size = totalsize - sent;
            if(send(ssock, buffer + sent, chunk_size, 0) <= 0){
                perror("send() buffer chunk");
                return -1;
            }

            int server_response;
            if(recv(ssock, &server_response, sizeof(server_response),0) <= 0){
                perror("recv() server read position");
                return -1;
            }
            sent = server_response;
        }
    }
    ////////////////////////////////////////////////////

    close(ssock);
    free(buffer);
    return 0;
}