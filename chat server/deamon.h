// daemon.h
#ifndef DAEMON_H
#define DAEMON_H
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
// 데몬화 함수 선언. 프로그램 이름 (argv[0])을 인자로 받을 수 있도록
int daemonize(int argc, char *argv[]);

#endif // DAEMON_H