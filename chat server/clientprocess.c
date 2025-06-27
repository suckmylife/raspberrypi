#include "clientprocess.h"


// --- 클라이언트 서버 (2차 자식) 프로세스의 메인 로직 함수 ---
// 이 함수는 fork()된 자식 프로세스에서 실행.
void client_work(pid_t client_pid, pid_t main_pid, int csock, int parent_pfd[2], int child_pfd[2]) {
    // 자식 프로세스 시그널 핸들러 설정
    setup_signal_handlers_child_main(); 

    // 부모->자식 파이프 (parent_pfd):
    // 자식은 이 파이프의 '읽기 끝'(parent_pfd[0])을 사용해서 부모 메시지를 받습니다.
    // 따라서 '쓰기 끝'(parent_pfd[1])은 자식에게 불필요하므로 닫습니다.
    close(parent_pfd[1]); 

    // 자식->부모 파이프 (child_pfd):
    // 자식은 이 파이프의 '쓰기 끝'(child_pfd[1])을 사용해서 부모에게 메시지를 보냅니다.
    // 따라서 '읽기 끝'(child_pfd[0])은 자식에게 불필요하므로 닫습니다.
    close(child_pfd[0]);

    // 자식 프로세스가 실제로 통신에 사용할 파일 디스크립터들을 명확히 정의합니다.
    int client_socket_fd = csock;           // 클라이언트와의 1대1 통신 소켓
    int read_from_parent_pipe_fd = parent_pfd[0]; // 부모로부터 메시지를 읽을 파이프 FD
    int write_to_parent_pipe_fd = child_pfd[1];   // 부모에게 메시지를 쓸 파이프 FD
    
    char child_mesg_buffer[BUFSIZ]; // 자식 프로세스 내부용 메시지 버퍼
    ssize_t child_n_read_write;

    // --- 자식 프로세스의 주된 통신 루프 ---
    // 이 루프 안에서 클라이언트와 부모로부터의 메시지를 지속적으로 확인하고 처리합니다.
    // O_NONBLOCK을 사용하므로, 각 read() 호출은 블로킹되지 않고 즉시 반환하며, 데이터가 없으면 EAGAIN을 반환합니다.
    // 시그널에 의해 read()가 EINTR로 중단되면, 루프가 다시 돌면서 플래그를 확인합니다.
    while (1) {
        // 1. 부모로부터 메시지가 도착했는지 확인 (SIGUSR1 시그널 플래그 이용)
        // 부모가 SIGUSR1을 보내면 child_sigusr_arrived 플래그가 설정됩니다.
        // 이 블록은 플래그가 설정되었을 때 실행됩니다. O_NONBLOCK 설정으로 블로킹을 피합니다.
        if (child_sigusr_arrived) {
            child_sigusr_arrived = 0; // 플래그를 초기화합니다.
            syslog(LOG_INFO, "Child %d: SIGUSR1 received, checking parent pipe for broadcast.", client_pid);
            
            // 부모 파이프 FD에서 메시지를 읽기 시도: 논블로킹이므로 데이터가 없으면 즉시 반환됩니다.
            child_n_read_write = read(read_from_parent_pipe_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);
            
            if (child_n_read_write > 0) {
                child_mesg_buffer[child_n_read_write] = '\0';
                syslog(LOG_INFO, "Child %d received from parent (broadcast): %s", client_pid, child_mesg_buffer);

                // 부모로부터 받은 메시지를 해당 클라이언트에게 전달합니다.
                // 클라이언트 소켓도 논블로킹으로 설정하고 쓰기 시도합니다.
                set_nonblocking(client_socket_fd);
                if (write(client_socket_fd, child_mesg_buffer, child_n_read_write) <= 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        //syslog(LOG_ERR, "Child %d failed to write to client (broadcast): %m", client_pid);
                        break; // 쓰기 오류 시 통신 루프 종료
                    }
                }
                set_blocking(client_socket_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
            } else if (child_n_read_write == 0) {
                // 부모 파이프가 닫힘 (부모 프로세스가 종료되었거나 파이프의 모든 쓰기 끝이 닫힘)
                syslog(LOG_INFO, "Child %d: Parent pipe closed. Exiting child loop.", client_pid);
                break; // 통신 루프 종료
            } else { // child_n_read_write < 0
                // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
                // EINTR은 시그널에 의해 중단된 것이므로 실제 오류가 아닙니다.
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                    //syslog(LOG_ERR, "Child %d read from parent pipe error: %m", client_pid);
                    break; // 다른 오류 발생 시 통신 루프 종료
                }
            }
        }

        // 2. 클라이언트 소켓에서 메시지 읽기 시도 (논블로킹)
        // O_NONBLOCK 설정으로 인해 클라이언트가 데이터를 보내지 않아도 블로킹되지 않고 즉시 반환합니다.
        // 부모가 SIGUSR1 시그널을 보내면 이 read()는 EINTR 오류로 중단될 수 있습니다.
        set_nonblocking(client_socket_fd);
        child_n_read_write = read(client_socket_fd, child_mesg_buffer, sizeof(child_mesg_buffer) - 1);
        set_blocking(client_socket_fd); // 읽기 후 다시 블로킹 모드로 복원합니다.

        if (child_n_read_write > 0) {
            child_mesg_buffer[child_n_read_write] = '\0'; // 문자열 종료 처리
            syslog(LOG_INFO, "Child %d received from client: %s", client_pid, child_mesg_buffer);

            // 클라이언트에게 받은 메시지를 부모에게 파이프를 통해 전달합니다.
            // 메시지 형식: "PID:메시지내용" (부모가 어떤 자식에게서 왔는지 알 수 있도록)
            char formatted_mesg[BUFSIZ + 32]; // PID 공간을 고려하여 버퍼 크기 증가
            snprintf(formatted_mesg, sizeof(formatted_mesg), "%d:%s", client_pid, child_mesg_buffer);
            ssize_t formatted_len = strlen(formatted_mesg);

            // 부모에게 메시지를 보내기 전, 부모에게 SIGUSR1 시그널을 전송합니다.
            // 이렇게 해야 부모의 accept()나 다른 read()가 EINTR로 중단되어 이 메시지를 처리할 수 있습니다.
            if (kill(main_pid, SIGUSR1) == -1) { // getppid() 대신 전달받은 main_pid 사용
                syslog(LOG_ERR, "Child %d: Failed to send SIGUSR1 to parent %d: %m", client_pid, main_pid);
            } 
            // 파이프에 메시지 쓰기 시도 (O_NONBLOCK 설정으로 버퍼가 가득 차면 블로킹되지 않고 즉시 반환합니다.)
            set_nonblocking(write_to_parent_pipe_fd);
            if (write(write_to_parent_pipe_fd, formatted_mesg, formatted_len + 1) <= 0) { // NULL 종료 문자 포함
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    //syslog(LOG_ERR, "Child %d failed to write to parent pipe: %m", client_pid);
                    break; // 쓰기 오류 시 통신 루프 종료
                }
            }
            set_blocking(write_to_parent_pipe_fd); // 쓰기 후 다시 블로킹 모드로 복원합니다.
        } else if (child_n_read_write == 0) {
            // 클라이언트 연결 종료 (EOF): 클라이언트가 연결을 끊었습니다.
            syslog(LOG_INFO, "Child %d: Client disconnected. Exiting child loop.", client_pid);
            break; // 통신 루프 종료
        } else { // child_n_read_write < 0
            // read 오류 발생. EAGAIN/EWOULDBLOCK은 논블로킹 모드에서 데이터가 없다는 의미입니다.
            // EINTR은 시그널에 의해 중단된 것이므로 실제 오류가 아닙니다.
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { 
                //syslog(LOG_ERR, "Child %d read from client error: %m", client_pid);
                break; // 다른 오류 발생 시 통신 루프 종료
            }
        }
        // CPU 과부하 방지를 위해 잠시 쉬어줍니다.
        // 논블로킹 모드에서는 CPU를 계속 소모할 수 있으므로, usleep은 필수적입니다.
        usleep(10000); // 10ms (10000 microseconds)
    } // --- while (1) 루프 종료 ---

    // 자식 프로세스 종료 전 모든 열린 파일 디스크립터를 닫습니다.
    // 자원 누수를 방지하고 운영체제에 FD를 반환합니다.
    close(client_socket_fd);
    close(read_from_parent_pipe_fd);
    close(write_to_parent_pipe_fd);
    syslog(LOG_INFO, "Child %d process exiting gracefully.", client_pid);
    exit(0); // 자식 프로세스는 자신의 역할을 마치면 반드시 종료합니다.
}