#include "comm.h"  
#include "clientprocess.h"
#include "sig.h"

// --- 전역 변수 정의 ---
roomInfo room_info[CHAT_ROOM]        = {0}; 
pipeInfo active_children[MAX_CLIENT] = {0}; 
volatile int num_active_children     = 0;
volatile int room_num                = 0; 

volatile sig_atomic_t parent_sigusr_arrived = 0; 
volatile sig_atomic_t child_sigusr_arrived  = 0;  
volatile sig_atomic_t child_exited_flag     = 0;

int main(int argc, char **argv)
{
    int ssock;   // 서버 소켓 (클라이언트 연결을 받을 때 사용)
    int csock;   // 클라이언트 소켓 (각 클라이언트와 1대1 통신)
    socklen_t cli_len; // 주소 구조체 길이를 저장할 변수 
    struct sockaddr_in servaddr, cliaddr; // 클라이언트의 주소정보를 담을 빈 그릇
    char mesg_buffer[BUFSIZ]; // 메시지 버퍼 (main 함수용)
    ssize_t n_read_write; // 읽거나 쓴 바이트 수

    // 메인 프로세스(부모)의 시그널 핸들러를 설정
    setup_signal_handlers_parent_main(); 

    // 데몬화 함수 호출 (argc, argv 인자 전달)
    daemonize(argc, argv); 

    // 서버 소켓 생성
    if((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        syslog(LOG_ERR, "socket not create: %m");
        exit(1);
    }
    // 서버 소켓 설정
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);

    // SO_REUSEADDR 옵션 설정: 서버 재시작 시 이전에 사용 중이던 포트를 즉시 재사용할 수 있게 합니다.
    int optval = 1;
    if (setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed: %m");
        close(ssock);
        exit(1);
    }

    // 서버 소켓 연결 (바인드): 소켓에 IP 주소와 포트 번호를 할당합니다.
    if(bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        syslog(LOG_ERR, "No Bind: %m");
        exit(1);
    }
    // 서버 소켓 가동 (리스닝): 클라이언트의 연결 요청을 대기하며, 최대 8개의 대기 연결 큐를 설정합니다.
    if(listen(ssock, 8) < 0){
        syslog(LOG_ERR, "Cannot listen: %m");
        exit(1);
    }

    // 서버 소켓(ssock)을 논블로킹 모드로 설정합니다.
    // 이렇게 하면 accept() 호출 시 대기 중인 연결이 없어도 블로킹되지 않고 즉시 반환됩니다.
    if (set_nonblocking(ssock) == -1) {
        syslog(LOG_ERR, "Failed to set ssock non-blocking: %m");
        exit(1);
    }

    cli_len = sizeof(cliaddr); 
    
    // --- 부모 프로세스의 메인 루프 (새 클라이언트 연결 수락 및 자식 관리) ---
    while(true) { 
        // 자식 종료 플래그가 설정되었다면, 종료된 자식을 정리합니다.
        if(child_exited_flag){
            clean_active_process(); // SIGCHLD 핸들러가 설정한 플래그를 확인하여 실제 정리 수행
            child_exited_flag = 0; // 플래그 초기화
        }

        // 클라이언트 연결 감지 및 수락: accept()는 논블로킹 소켓이므로, 연결이 없으면 EAGAIN/EWOULDBLOCK을 반환합니다.
        // 시그널에 의해 중단되면 EINTR을 반환합니다.
        csock = accept(ssock, (struct sockaddr *)&cliaddr, &cli_len);
        if (csock < 0) {
            // accept()가 오류를 반환했을 때의 처리
            if (errno == EINTR) { 
                syslog(LOG_INFO, "Parent: accept() interrupted by signal (EINTR).");
                // 시그널에 의해 깨어났으니, 자식으로부터 온 메시지를 확인하는 로직으로 넘어갑니다.
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 논블로킹 모드에서 현재 대기 중인 연결이 없는 경우
                usleep(10000); 
                //syslog(LOG_INFO, "Parent: No pending connections, accept() returned EAGAIN/EWOULDBLOCK.");
            } else {
                syslog(LOG_ERR, "accept() error: %m");
                break; 
            }

            // 시그널에 의해 중단되었거나, 연결이 없어 EAGAIN/EWOULDBLOCK이 발생한 경우
            // 여기서 자식들로부터 온 메시지를 확인하는 로직을 수행합니다.
            if (parent_sigusr_arrived) { 
                parent_sigusr_arrived = 0; 
                syslog(LOG_INFO, "Parent: Checking for messages from children.");
                for (int i = 0; i < num_active_children; i++) {
                    if (!active_children[i].isActive) {
                        continue; 
                    }
                    n_read_write = read(active_children[i].child_to_parent_read_fd, mesg_buffer, sizeof(mesg_buffer) - 1);
                    
                    if (n_read_write > 0) {
                        mesg_buffer[n_read_write] = '\0';
                        syslog(LOG_INFO, "Parent received message from child %d: %s", active_children[i].pid, mesg_buffer);
                        /*
                            strchr 함수는 content라는 문자열(데이터 덩어리) 안에서 
                            '\n'이라는 문자를 찾아. 그리고 그 '\n' 문자가 메모리 상의 
                            어디에 있는지, 그 '주소'를 찾아내서 알려줌
                        */
                        char *pid_str  = strtok(mesg_buffer, ":");
                        char *content  = strtok(NULL, "");
                        char *rm_enter = strchr(content, '\n');
                        if(rm_enter) *rm_enter = '\0';

                        if (pid_str && content) {
                            pid_t from_who = atoi(pid_str); 
                            
                            if (content[0] == '/') {
                                int isAdd     = check_command(content, "add");
                                int isJoin    = check_command(content, "join");
                                int isRm      = check_command(content, "rm"); 
                                int isList    = check_command(content, "list"); 
                                int isUsers   = check_command(content, "users");

                                if (isAdd) {
                                    if (room_num < CHAT_ROOM) {
                                        strncpy(room_info[room_num].name, content + 2 + strlen("add"), NAME - 1);
                                        room_info[room_num].name[NAME - 1] = '\0'; 
                                        syslog(LOG_INFO, "Parent: Room '%s' created.", room_info[room_num].name);
                                        room_num++;
                                    } else {
                                        syslog(LOG_WARNING, "Parent: Max chat rooms reached. Cannot create room '%s'.", content + 2 + strlen("add"));
                                    }
                                } else if (isJoin) {
                                    char *join_room_name = content + 2 + strlen("join");
                                    int client_idx = -1;
                                    //누가 이 명령어 썼냐, 쓴 클라이언트에게 부여하기 위한 검색 작업
                                    for(int k=0; k<num_active_children; k++){
                                        if(active_children[k].pid == from_who){
                                            client_idx = k;
                                            break;
                                        }
                                    }
                                    //그 클라이언트의 구조체에 채팅방 정보 저장
                                    if(client_idx != -1){
                                        strncpy(active_children[client_idx].room_name, join_room_name, NAME - 1);
                                        active_children[client_idx].room_name[NAME - 1] = '\0';
                                        syslog(LOG_INFO, "Parent: Client %d ('%s') joined room '%s'.", from_who, active_children[client_idx].name, active_children[client_idx].room_name);
                                    } else {
                                        syslog(LOG_ERR, "Parent: Could not find client with PID %d to join room.", from_who);
                                    }
                                } else if(isRm){
                                    /*
                                        content + 2 + 2가 되어 content + 4와 같음 (포인터 연산)
                                        즉, 원본 문자열의 처음 4글자를 건너뛰어라

                                        연산자로 문자열을 비교하면 문자열의 내용이 
                                        아니라 문자열의 주소(포인터)를 비교
                                    */
                                    char *rm_room_name = content + 2 + strlen("rm");
                                    //pipinfo에서 채팅방 정보 삭제
                                    for(int k=0; k<num_active_children; k++){
                                        if(strcmp(active_children[k].room_name, rm_room_name)==0){
                                            /*
                                            C언어에서 배열 이름은 곧 그 배열의 첫 번째 요소의 주소(포인터)로 
                                            취급. 배열 자체를 통째로 = 연산자로 복사할 수 없음
                                            */
                                            strcpy(active_children[k].room_name, "");
                                            syslog(LOG_INFO, "Parent: Remove Room Info" );
                                            
                                        }
                                    }
                                    //roomInfo에서 채팅방 목록에서 삭제
                                    for(int k=0; k<room_num; k++){
                                        if(strcmp(room_info[k].name, rm_room_name)==0){
                                            if(k < room_num-1){
                                                for(int j = k; j<room_num-1; j++){
                                                    room_info[j] = room_info[j+1];
                                                    syslog(LOG_INFO, "Shifted: room at index %d is now '%s'", j, room_info[j].name);
                                                }
                                            }
                                            room_num--;
                                            break;
                                        }
                                    }
                                }else if(isList){//방 리스트 목록
                                    int client_idx = -1;
                                    //누가 이 명령어 썼냐, 쓴 클라이언트에게 부여하기 위한 검색 작업
                                    for(int k=0; k<num_active_children; k++){
                                        if(active_children[k].pid == from_who){
                                            client_idx = k;
                                            break;
                                        }
                                    }
                                    //리스트 목록 작성하기
                                    for(int k=0; k<room_num; k++){
                                        syslog(LOG_INFO, "Parent: Show Room List %d : ('%s')",k,room_info[k].name);
                                        //strnlen : 보통 버퍼 크기가 정해져 있을 때, 그 크기를 넘지 않고 문자열 길이를 안전하게 구함
                                        size_t name_len = strnlen(room_info[k].name, sizeof(room_info[k].name));
                                        ssize_t wlen = write(active_children[client_idx].parent_to_child_write_fd, room_info[k].name, name_len);
                                        if ( wlen <= 0) { 
                                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                                    //syslog(LOG_ERR, "Parent failed to broadcast to child %d: %m", active_children[j].pid);
                                            }
                                        }else if(wlen > 0){
                                            if (kill(active_children[client_idx].pid, SIGUSR1) == -1) {
                                                syslog(LOG_ERR, "Parent: (list) Failed to send SIGUSR1 to child %d: %m", active_children[client_idx].pid);
                                            } 
                                        }
                                    }
                                }
                                /////////////////////////////////////////////////////////////////////////////
                                ///////////////     유저 고유 명령어  /////////////////////////////////////////
                                /////////////////////////////////////////////////////////////////////////////
                                else if(isUsers){ //채팅방에 참여하는 사용자 목록수 
                                    int client_idx = -1;
                                    //누가 이 명령어 썼냐, 쓴 클라이언트에게 부여하기 위한 검색 작업
                                    for(int k=0; k<num_active_children; k++){
                                        if(active_children[k].pid == from_who){
                                            client_idx = k;
                                            break;
                                        }
                                    }

                                    if(client_idx != -1){ //이 명령어를 쓴 유저에게 현재 채팅방의 유저를 알려준다. 
                                        for(int k=0; k<num_active_children; k++){
                                            if(active_children[k].room_name == active_children[client_idx].room_name){
                                                size_t name_len = strnlen(active_children[k].name, sizeof(active_children[k].name));
                                                ssize_t wlen = write(active_children[client_idx].parent_to_child_write_fd, active_children[k].name, name_len);
                                                if ( wlen <= 0) { 
                                                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                                            //syslog(LOG_ERR, "Parent failed to broadcast to child %d: %m", active_children[j].pid);
                                                    }
                                                }else if(wlen > 0){
                                                    if (kill(active_children[client_idx].pid, SIGUSR1) == -1) {
                                                        syslog(LOG_ERR, "Parent: (users) Failed to send SIGUSR1 to child %d: %m", active_children[client_idx].pid);
                                                    } 
                                                }
                                            }
                                        }
                                    }else{
                                        syslog(LOG_ERR, "this user no exist");
                                    }
                                }
                            } 
                            else if (active_children[i].name[0] == '\0') { 
                                strncpy(active_children[i].name, content, NAME - 1);
                                active_children[i].name[NAME - 1] = '\0';
                                syslog(LOG_INFO, "Parent: Client %d set name to '%s'.", from_who, active_children[i].name);
                            }
                            else if (content[0] == '!' && check_command(content, "whisper")){ //귓속말일때
                                char *c = content + 2 + strlen("whisper");
                                char user_name[BUFSIZ];
                                char mesg[BUFSIZ];
                                char origin[BUFSIZ]; // 원본 문자열을 보존하기 위한 복사본
                                strcpy(origin, c); // '!whisper+ "공백" ' 건너뛰기
                                //strtok는 원본을 훼손함
                                char *token = strtok(origin, " "); // origin에서 첫번째 공백까지 잘라라
                                if (token != NULL) {
                                    strcpy(user_name, token);
                                    // 나머지 부분을 메시지로 저장
                                    token = strtok(NULL, ""); // 첫번째 공백까지 잘라진 나머지 부분을 가져와라
                                    if (token != NULL) {
                                        strcpy(mesg, token);
                                    }else {
                                        mesg[0] = '\0';
                                    }
                                }
                                
                                int client_idx = -1;
                                //누가 이 명령어 썼냐, 쓴 클라이언트에게 부여하기 위한 검색 작업
                                for(int k=0; k<num_active_children; k++){
                                    if(active_children[k].pid == from_who){
                                        client_idx = k;
                                        break;
                                    }
                                }

                                if(client_idx != -1){ //이 명령어를 쓴 유저가 귓속말 하려는 유저에게 write 
                                    for(int k=0; k<num_active_children; k++){
                                        if(strcmp(active_children[k].name, user_name) == 0){
                                            
                                            char final_message[BUFSIZ];
                                            size_t name_len = strnlen(final_message, sizeof(final_message));
                                            snprintf(final_message, sizeof(final_message), "from %s : %s", (int)name_len,active_children[client_idx].name, mesg);
                                            ssize_t wlen = write(active_children[client_idx].parent_to_child_write_fd, final_message, name_len);
                                            if ( wlen <= 0) { 
                                                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                                        //syslog(LOG_ERR, "Parent failed to broadcast to child %d: %m", active_children[j].pid);
                                                }
                                            }else if(wlen > 0){
                                                if (kill(active_children[client_idx].pid, SIGUSR1) == -1) {
                                                    syslog(LOG_ERR, "Parent: (whisper) Failed to send SIGUSR1 to child %d: %m", active_children[client_idx].pid);
                                                } 
                                            }
                                            break;
                                        }
                                    }
                                }else{
                                    syslog(LOG_ERR, "this user no exist");
                                }
                                
                            }
                            else { 
                                char broadcast_mesg[BUFSIZ + NAME + 10]; 
                                snprintf(broadcast_mesg, sizeof(broadcast_mesg), "%s: %s", active_children[i].name, content);
                                ssize_t broadcast_len = strlen(broadcast_mesg);

                                char *sender_room_name = active_children[i].room_name;
                                if (sender_room_name[0] == '\0') { 
                                    syslog(LOG_INFO, "Parent: Message from client %d ('%s') but not in a room. Message: %s", from_who, active_children[i].name, content);
                                    continue; 
                                }
                                //부모가 해당 채팅방에 브로드캐스트 하는 곳 
                                for (int j = 0; j < num_active_children; j++) {
                                    if (active_children[j].isActive && 
                                        (strcmp(active_children[j].room_name, sender_room_name) == 0)) 
                                    {
                                        syslog(LOG_INFO, "Parent broadcasting to client %d ('%s') in room '%s'. Message: %s", active_children[j].pid, active_children[j].name, active_children[j].room_name, broadcast_mesg);
                                        ssize_t wlen = write(active_children[j].parent_to_child_write_fd, broadcast_mesg, broadcast_len + 1);
                                        if ( wlen <= 0) { 
                                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                                    //syslog(LOG_ERR, "Parent failed to broadcast to child %d: %m", active_children[j].pid);
                                            }
                                        }else if(wlen > 0){
                                            if (kill(active_children[j].pid, SIGUSR1) == -1) {
                                                syslog(LOG_ERR, "Parent: (broad cast) Failed to send SIGUSR1 to child %d: %m", active_children[j].pid);
                                            } 
                                        }
                                    }
                                }
                            }
                        } else {
                            syslog(LOG_WARNING, "Parent: Received malformed message from child %d: %s", active_children[i].pid, mesg_buffer);
                        }
                    } else if (n_read_write == 0) {
                        syslog(LOG_INFO, "Parent: Child %d pipe closed (detected during read scan).", active_children[i].pid);
                    } else { 
                        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                            //syslog(LOG_ERR, "Parent read from child %d pipe error: %m", active_children[i].pid);
                        }
                    }
                }
            }
            continue; 
        }

        // --- 새로운 클라이언트 연결 처리 (csock >= 0 인 경우) ---

        if (num_active_children >= MAX_CLIENT) {
            syslog(LOG_WARNING, "MAX_CLIENT limit reached. Closing new connection from %s.", inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_buffer, BUFSIZ));
            close(csock); 
            continue; 
        }

        inet_ntop(AF_INET, &cliaddr.sin_addr, mesg_buffer, BUFSIZ);
        syslog(LOG_INFO, "Client is connected : %s", mesg_buffer);

        int parent_pfd[2]; 
        int child_pfd[2];  

        if(pipe(child_pfd) < 0){ 
            syslog(LOG_ERR, "Failed to create child->parent pipe: %m");
            close(csock); 
            continue; 
        }
        if(pipe(parent_pfd) < 0){ 
            syslog(LOG_ERR, "Failed to create parent->child pipe: %m");
            close(child_pfd[0]); close(child_pfd[1]); 
            close(csock); 
            continue; 
        }

        if (set_nonblocking(parent_pfd[0]) == -1 || set_nonblocking(parent_pfd[1]) == -1 ||
            set_nonblocking(child_pfd[0]) == -1 || set_nonblocking(child_pfd[1]) == -1) {
            syslog(LOG_ERR, "Failed to set pipe FDs non-blocking: %m");
            close(csock);
            close(parent_pfd[0]); close(parent_pfd[1]);
            close(child_pfd[0]);  close(child_pfd[1]);
            continue;
        }

        pid_t pids_; 
        if((pids_ = fork()) < 0){ 
            syslog(LOG_ERR, "fork failed: %m");
            close(csock);
            close(parent_pfd[0]); close(parent_pfd[1]);
            close(child_pfd[0]);  close(child_pfd[1]);
            continue; 
        }
        // --- 자식 프로세스 ---
        else if(pids_ == 0){ 
            syslog(LOG_INFO, "Child process started for PID %d.", getpid());
            
            // 자식은 서버 리스닝 소켓을 사용하지 않으므로 닫습니다.
            // ssock은 main 함수의 로컬 변수지만, fork()에 의해 FD가 복제되었으므로 자식 프로세스에서 닫을 수 있습니다.
            close(ssock); 

            // client_work 함수로 제어권을 넘깁니다.
            // client_work 함수 내에서 필요한 시그널 핸들러 설정과 파이프 FD 정리가 이루어집니다.
            client_work(getpid(), getppid(), csock, parent_pfd, child_pfd);
            // client_work 내부에서 exit(0) 호출로 자식 프로세스가 종료되므로, 이 이후의 코드는 실행되지 않습니다.
        }
        // --- 부모 프로세스 ---
        else { 
            close(csock); 

            close(parent_pfd[0]); 
            close(child_pfd[1]);

            if (num_active_children < MAX_CLIENT) {
                active_children[num_active_children].pid = pids_; 
                active_children[num_active_children].parent_to_child_write_fd = parent_pfd[1]; 
                active_children[num_active_children].child_to_parent_read_fd = child_pfd[0];   
                active_children[num_active_children].isActive = true; 
                memset(active_children[num_active_children].name, 0, NAME);
                memset(active_children[num_active_children].room_name, 0, NAME);

                syslog(LOG_INFO, "Parent: Child %d added. Total active children: %d.", pids_, num_active_children + 1);
                num_active_children++; 
            } else {
                syslog(LOG_WARNING, "Parent: MAX_CLIENT limit reached. Not managing child %d.", pids_);
                close(parent_pfd[1]); 
                close(child_pfd[0]);
                kill(pids_, SIGTERM); 
            }
        }
    } 
    
    // --- 서버 종료 로직 (Graceful Shutdown) ---
    for (int i = 0; i < num_active_children; i++) {
        syslog(LOG_INFO, "Parent: Sending SIGTERM to child %d.", active_children[i].pid);
        kill(active_children[i].pid, SIGTERM);
        close(active_children[i].parent_to_child_write_fd);
        close(active_children[i].child_to_parent_read_fd);
    }
    while (wait(NULL) > 0);
    
    close(ssock); 
    syslog(LOG_INFO, "Server shutting down gracefully.");

    return 0;
}
