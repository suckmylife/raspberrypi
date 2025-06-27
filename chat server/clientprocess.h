#ifndef CLIENTPROCESS_H
#define CLIENTPROCESS_H

#include "comm.h"

void client_work(pid_t client_pid, pid_t main_pid, \
                 int csock, int parent_pfd[2], int child_pfd[2]);

#endif //CLIENTPROCESS_H