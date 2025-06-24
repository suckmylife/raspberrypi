#ifndef CLIENTMANAGER_H
#define CLIENTMANAGER_H
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "common.h"

void client_work(int client_sock_fd, int client_read_pipe_fd, int client_write_pipe_fd);

#endif //CLIENTMANAGER_H