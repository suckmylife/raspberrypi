#include "comm.h"

// --- FCNTL 관련 함수 ---
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); 
    if (flags == -1) {
        syslog(LOG_ERR, "fcntl(F_GETFL) error for fd %d: %m", fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "fcntl(F_SETFL, O_NONBLOCK) error for fd %d: %m", fd);
        return -1;
    }
    return 0;
}

int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); 
    if (flags == -1) {
        syslog(LOG_ERR, "fcntl(F_GETFL) error for fd %d: %m", fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "fcntl(F_SETFL, blocking) error for fd %d: %m", fd);
        return -1;
    }
    return 0;
}

// --- 명령어 검사 함수 ---
int check_command(const char* mesg, const char* command){
    if (mesg[0] != '/') {
        return 0;
    }
    if (strncmp(mesg + 1, command, strlen(command)) == 0) {
        char char_after_command = mesg[1 + strlen(command)];
        if (char_after_command == ' ' || char_after_command == '\0' || char_after_command == '\n') {
            return 1; 
        }
    }
    return 0;
}