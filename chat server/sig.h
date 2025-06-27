#ifndef SIG_H
#define SIG_H

#include "comm.h"

//자식에게 받은 신호
void handle_parent_sigusr(int signum);
//부모에게 받은 신호
void handle_child_sigusr(int signum);
//자식이 죽은 신호
void handle_sigchld_main(int signum);
//죽었을때 열린 파이프 및 각종 메모리 해제 담당
void clean_active_process();
// --- 시그널 핸들러 등록 함수 ---
void setup_signal_handlers_parent_main();
void setup_signal_handlers_child_main();

#endif //SIG_H