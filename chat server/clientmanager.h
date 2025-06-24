#ifndef CLIENTMANAGER_H
#define CLIENTMANAGER_H
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "common.h"

void client_work(pid_t client_server_pid,pid_t main_server_pid,int client_sock_fd, int client_read_pipe_fd, int client_write_pipe_fd);

#endif //CLIENTMANAGER_H