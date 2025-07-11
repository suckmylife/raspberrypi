#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/videodev2.h>

#define VIDEO_DEVICE        "/dev/video0"
#define FRAMEBUFFER_DEVICE  "/dev/fb0"
#define WIDTH               640
#define HEIGHT              480

static struct fb_var_screeninfo vinfo;

void display_frame(uint16_t *fbp, uint8_t *data, int width, int height) 
{
  int x_offset = (vinfo.xres - width) / 2;
  int y_offset = (vinfo.yres - height) / 2;

  // YUYV -> RGB565 변환하여 프레임버퍼에 출력
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; x += 2) {
      uint8_t Y1 = data[(y * width + x) * 2];
      uint8_t U = data[(y * width + x) * 2 + 1];
      uint8_t Y2 = data[(y * width + x + 1) * 2];
      uint8_t V = data[(y * width + x + 1) * 2 + 1];

      int R1 = Y1 + 1.402 * (V - 128);
      int G1 = Y1 - 0.344136 * (U - 128) - 0.714136 * (V - 128);
      int B1 = Y1 + 1.772 * (U - 128);

      int R2 = Y2 + 1.402 * (V - 128);
      int G2 = Y2 - 0.344136 * (U - 128) - 0.714136 * (V - 128);
      int B2 = Y2 + 1.772 * (U - 128);

      // RGB565 포맷으로 변환 (R: 5비트, G: 6비트, B: 5비트)
      uint16_t pixel1 = ((R1 & 0xF8) << 8) | ((G1 & 0xFC) << 3) | (B1 >> 3);
      uint16_t pixel2 = ((R2 & 0xF8) << 8) | ((G2 & 0xFC) << 3) | (B2 >> 3);

      fbp[(y + y_offset) * vinfo.xres + (x + x_offset)] = pixel1;
      fbp[(y + y_offset) * vinfo.xres + (x + x_offset + 1)] = pixel2;
    }
  }
}

int main() 
{
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
/*
  // 버퍼 설정
  struct v4l2_requestbuffers req;
  req.count = 1;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;
  if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
    perror("Failed to request buffer");
    close(fd);
    return 1;
  }
*/
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
    int ret = read(fd, buffer, fmt.fmt.pix.sizeimage);
    if (ret == -1) {
      perror("Failed to read frame");
      break;
    }

    // buffer에 읽어온 프레임 데이터를 처리
    printf("Captured frame size: %d bytes\n", ret);
    display_frame(fbp, buffer, WIDTH, HEIGHT);
  }

  free(buffer);
  close(fd);

  return 0;
}