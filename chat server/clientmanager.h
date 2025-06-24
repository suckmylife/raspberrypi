#ifndef CLIENTMANAGER_H
#define CLIENTMANAGER_H
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>

#include "common.h"
#include "signals.h"

void client_work(pid_t client_server_pid,pid_t main_server_pid,int client_sock_fd, int main_to_client_pipe_fds[2], int client_to_main_pipe_fds[2]);

#endif //CLIENTMANAGER_H