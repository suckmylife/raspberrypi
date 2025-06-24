#include "common.h"

volatile sig_atomic_t is_write_from_chat_room = 0;
volatile sig_atomic_t is_write_from_client = 0;
volatile sig_atomic_t is_shutdown = 0;
volatile sig_atomic_t child_exited_flag = 0;
volatile sig_atomic_t client_num = 0;
volatile sig_atomic_t room_num = 0;
roomInfo room_info[CHAT_ROOM];          // 실제 메모리 할당 및 초기화 (필요 시)
pipeInfo client_pipe_info[CHAT_ROOM];

