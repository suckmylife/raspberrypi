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
#define WIDTH               640
#define HEIGHT              480

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

    //fgets(mesg,BUFSIZ,stdin);
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
    
    while (1) {
        int totalsize = read(fd, buffer, fmt.fmt.pix.sizeimage);
        if (totalsize <= 0) {
            perror("Failed to read frame");
            break;
        }
        printf("totalsize : %d\n", totalsize);
        
        // 총 사이즈 보내기
        if (send(ssock, &totalsize, sizeof(totalsize), 0) <= 0) {
            perror("send()");
            break;
        }
        
        int server_read;
        if (recv(ssock, &server_read, sizeof(server_read), 0) <= 0) {
            perror("recv");
            break;
        }
        
        // 버퍼 전송
        int sent = 0;
        while (sent < totalsize) {
            // 적절한 청크 크기 설정 (예: 8KB)
            int chunk_size = (totalsize - sent > 8192) ? 8192 : (totalsize - sent);
            
            int bytes_sent = send(ssock, buffer + sent, chunk_size, 0);
            if (bytes_sent <= 0) {
                perror("send() buffer chunk");
                break;
            }
            
            sent += bytes_sent;
            
            // 서버에 현재 위치 알림
            if (send(ssock, &sent, sizeof(sent), 0) <= 0) {
                perror("send() current position");
                break;
            }
            
            // 서버 응답 확인 (선택적)
            int server_response;
            if (recv(ssock, &server_response, sizeof(server_response), 0) <= 0) {
                perror("recv() server read position");
                break;
            }
        }

        // 전송 실패 시 루프 종료
        if (sent < totalsize) {
            break;
        }
    }
    ////////////////////////////////////////////////////

    close(ssock);
    free(buffer);
    return 0;
}