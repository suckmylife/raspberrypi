#include "common.h"

volatile sig_atomic_t is_write_from_chat_room = 0;
volatile sig_atomic_t is_write_from_client = 0;
volatile sig_atomic_t is_shutdown = 0;
volatile sig_atomic_t child_exited_flag = 0;
volatile sig_atomic_t client_num = 0;
volatile sig_atomic_t room_num = 0;
