// daemon.h
#ifndef DAEMON_H
#define DAEMON_H

// 데몬화 함수 선언. 프로그램 이름 (argv[0])을 인자로 받을 수 있도록
void daemonize(const char *program_name);

#endif // DAEMON_H