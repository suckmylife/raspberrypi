// #include <stdio.h>
// #include <unistd.h>
// #include <string.h>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <fcntl.h>
// #include <stdlib.h>
// #include <stdint.h>
// #include <sys/ioctl.h>
// #include <sys/mman.h>
// #include <linux/fb.h>
// #include <linux/videodev2.h>

// #define TCP_PORT 5100


// /* 비디오 */
// #define VIDEO_DEVICE        "/dev/video0"
// #define WIDTH               640
// #define HEIGHT              480

// /* 비디오 */

// int main(int argc, char **argv)
// {
//     int ssock;
//     int buffer_size = 1024 * 1024; // 1MB
//     setsockopt(ssock, SOL_SOCKET, SO_RCVBUF, (char *)&buffer_size, sizeof(buffer_size));
//     setsockopt(ssock, SOL_SOCKET, SO_SNDBUF, (char *)&buffer_size, sizeof(buffer_size));
//     struct sockaddr_in servaddr, cliaddr;
//     char mesg[BUFSIZ];

//     if(argc < 2){
//         printf("Usage : %s IP_ADDRESS \n",argv[0]);
//         return -1;
//     }

//     if((ssock = socket(AF_INET,SOCK_STREAM, 0))<0){
//         perror("socket()");
//         return -1;
//     }

//     memset(&servaddr,0,sizeof(servaddr));
//     servaddr.sin_family = AF_INET;

//     inet_pton(AF_INET, argv[1],&(servaddr.sin_addr.s_addr));
//     servaddr.sin_port = htons(TCP_PORT);

//     if(connect(ssock,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0){
//         perror("connect()");
//         return -1;
//     }
//     // 클라이언트 코드에서
//     int flags = fcntl(ssock, F_GETFL, 0);
//     fcntl(ssock, F_SETFL, flags | O_NONBLOCK);
//     //fgets(mesg,BUFSIZ,stdin);
//     ////////////////////////////////////////////////////
//     int fd = open(VIDEO_DEVICE, O_RDWR);
//     if (fd == -1) {
//         perror("Failed to open video device");
//         return 1;
//     }

//     struct v4l2_format fmt;
//     fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
//     fmt.fmt.pix.width = WIDTH;
//     fmt.fmt.pix.height = HEIGHT;
//     fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
//     fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

//     if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
//         perror("Failed to set format");
//         close(fd);
//         return 1;
//     }

//     char *buffer = malloc(fmt.fmt.pix.sizeimage);
//     if (!buffer) {
//         perror("Failed to allocate buffer");
//         close(fd);
//         return 1;
//     }
    
//     while (1) {
//         int totalsize = read(fd, buffer, fmt.fmt.pix.sizeimage);
//         if (totalsize <= 0) {
//             perror("Failed to read frame");
//             break;
//         }
//         printf("totalsize : %d\n", totalsize);
        
//         // 총 사이즈 보내기
//         if (send(ssock, &totalsize, sizeof(totalsize), 0) <= 0) {
//             perror("send()");
//             break;
//         }
        
//         // 버퍼 전송
//         int sent = 0;
//         while (sent < totalsize) {
//             // 적절한 청크 크기 설정 (예: 8KB)
//             int chunk_size = (totalsize - sent > 4000) ? 4000 : (totalsize - sent);
            
//             int bytes_sent = send(ssock, buffer + sent, chunk_size, 0);
//             if (bytes_sent <= 0) {
//                 perror("send() buffer chunk");
//                 break;
//             }
            
//             sent += bytes_sent;
//         }

//         // 3. 모든 데이터 전송 후, 서버로부터 최종 완료 응답을 기다립니다.
//         int final_server_response;
//         if (recv(ssock, &final_server_response, sizeof(final_server_response), 0) <= 0) {
//             perror("recv() final server response");
//             break;
//         }

//         // 서버가 모든 데이터를 성공적으로 받았음을 확인 (예: 1을 보냈을 경우)
//         if (final_server_response == 1) {
//             printf("Frame sent successfully and confirmed by server.\n");
//         } else {
//             fprintf(stderr, "Server did not confirm successful reception.\n");
//             break;
//         }
//     }
//     ////////////////////////////////////////////////////

//     close(ssock);
//     free(buffer);
//     return 0;
// }

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>  // EAGAIN, EWOULDBLOCK 오류 처리를 위한 헤더
#include <sys/time.h>  // select() 함수 사용을 위한 헤더

#define TCP_PORT 5100

int main(int argc, char **argv) {
    int ssock;
    struct sockaddr_in servaddr;
    
    // 소켓 생성
    if((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    
    // 서버 주소 설정
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  // 서버 IP 주소
    servaddr.sin_port = htons(TCP_PORT);
    
    // 서버에 연결
    if(connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }
    
    // 소켓 버퍼 크기 증가
    int buffer_size = 1024 * 1024;  // 1MB
    setsockopt(ssock, SOL_SOCKET, SO_RCVBUF, (char *)&buffer_size, sizeof(buffer_size));
    setsockopt(ssock, SOL_SOCKET, SO_SNDBUF, (char *)&buffer_size, sizeof(buffer_size));
    
    // 논블로킹 모드로 설정
    int flags = fcntl(ssock, F_GETFL, 0);
    fcntl(ssock, F_SETFL, flags | O_NONBLOCK);
    
    // 프레임 전송 루프
    while (1) {
        int totalsize = read(fd, buffer, fmt.fmt.pix.sizeimage);  // 클라이언트에서 프레임 읽기
        if (totalsize <= 0) {
            perror("Failed to read frame");
            break;
        }
        printf("totalsize : %d\n", totalsize);
        
        // 1. 총 사이즈 보내기
        int send_result;
        while ((send_result = send(ssock, &totalsize, sizeof(totalsize), 0)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 송신 버퍼가 가득 찼을 때 잠시 대기
                usleep(1000);  // 1ms 대기
                continue;
            } else {
                // 실제 오류 발생
                perror("send() totalsize");
                goto cleanup;
            }
        }
        
        // 2. 버퍼 전송 (서버 응답을 기다리지 않고 연속적으로 전송)
        int sent = 0;
        while (sent < totalsize) {
            int chunk_size = (totalsize - sent > 8192) ? 8192 : (totalsize - sent);
            
            int bytes_sent;
            while ((bytes_sent = send(ssock, buffer + sent, chunk_size, 0)) < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 송신 버퍼가 가득 찼을 때 잠시 대기
                    usleep(1000);  // 1ms 대기
                    continue;
                } else {
                    // 실제 오류 발생
                    perror("send() buffer chunk");
                    goto cleanup;
                }
            }
            
            sent += bytes_sent;
        }
        
        // 3. 서버로부터 최종 완료 응답을 기다림
        int final_server_response;
        fd_set readfds;
        struct timeval tv;
        
        // select()를 사용하여 데이터가 도착할 때까지 효율적으로 대기
        FD_ZERO(&readfds);
        FD_SET(ssock, &readfds);
        tv.tv_sec = 5;  // 5초 타임아웃
        tv.tv_usec = 0;
        
        int activity = select(ssock + 1, &readfds, NULL, NULL, &tv);
        
        if (activity < 0) {
            perror("select()");
            break;
        } else if (activity == 0) {
            // 타임아웃 발생
            printf("Timeout waiting for server response\n");
            break;
        }
        
        // 데이터가 도착했으므로 recv() 호출
        int recv_result;
        while ((recv_result = recv(ssock, &final_server_response, sizeof(final_server_response), 0)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 데이터가 아직 없음, 잠시 대기
                usleep(1000);  // 1ms 대기
                continue;
            } else {
                // 실제 오류 발생
                perror("recv() final server response");
                goto cleanup;
            }
        }
        
        if (recv_result == 0) {
            // 연결이 종료됨
            printf("Server closed connection\n");
            break;
        }
        
        // 서버가 모든 데이터를 성공적으로 받았음을 확인
        if (final_server_response == 1) {
            printf("Frame sent successfully and confirmed by server.\n");
        } else {
            fprintf(stderr, "Server did not confirm successful reception.\n");
            break;
        }
    }
    
cleanup:
    close(ssock);
    return 0;
}