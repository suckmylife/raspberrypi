#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

#define FBDVICE "/dev/fb0"

int main(int argc, char **argv)
{
    int fbfd = 0;
    struct fb_var_screeninfo vinfo, old_vinfo;
    sturct fb_fix_screeninfo finfo;

    fbfd = open(FBDVICE, O_RDWR);
    if(fbfd < 0){
        perror("Error : cannot open framebuffer device");
        return -1;
    }

    if(ioctl(fbfd,FBIOGET_FSCREENINFO, &finfo) < 0){
        perror("Error reading fixed information");
        return -1;
    }

    if(ioctl(fbfd,FBIOGET_VSCREENINFO, &vinfo) < 0){
        perror("Error reading variable inforamtion");
        return -1;
    }

    printf("Resolution : %dx%d, %dbpp\n",vinfo.xres,vinfo.yres,vinfo.bits_per_pixel);
    printf("Virtual Resolution : %dx%d\n",vinfo.xres_virtual,vinfo.yres_virtual);
    printf("Length of frame buffer memory : %d\n", finfo.smem_len);
    
    old_vinfo = vinfo;

    vinfo.xres = 800;
    vinfo.yres = 600;

    if(ioctl(fbfd,FBIOPUT_VSCREENINFO,&vinfo) < 0){
        perror("fbdev ioctl(PUT)");
        return -1;
    }
    printf("New Resolution : %dx%d, %dbpp\n",vinfo.xres,vinfo.yres,vinfo.bits_per_pixel);
    getchar();
    ioctl(fbfd,FBIOPUT_VSCREENINFO, &old_vinfo);
    close(fbfd);
    return 0;
}