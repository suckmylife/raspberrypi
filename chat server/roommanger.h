#ifndef ROOMMANAGER_H
#define ROOMMANAGER_H

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "common.h"

socklen_t chat_len;
struct sockaddr_in chataddr;

pid_t chat_pid; //부모 자식 구분자
int chat_parent_pfd[2]; //부모->자식 fd
int chat_child_pfd[2];  //자식->부모 fd 

#endif // ROOMMANAGER_H