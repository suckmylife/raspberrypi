#include "clientmanager.h"

void client_work(pid_t client_server_pid,pid_t main_server_pid, int client_sock_fd, int main_to_client_pipe_fds[2], int client_to_main_pipe_fds[2])
{
    char mesg[BUFSIZ]; //메시지 읽는거
    ssize_t n,client_n;
    close(main_to_client_pipe_fds[1]);
    close(client_to_main_pipe_fds[0]);
    while(!is_shutdown)
    {

        n = recv(client_sock_fd, mesg, BUFSIZ-1, 0);
        //클라이언트에서 메시지를 받는다
        if(n > 0){
            char add_mesg[BUFSIZ];
            mesg[n] = '\0';
            //syslog(LOG_INFO,"Received Client data : %s",mesg);
            snprintf(add_mesg, BUFSIZ, "%d:%s", client_server_pid, mesg);

            /////test
            // int flags = fcntl(client_to_main_pipe_fds[1], F_GETFL);
            // if (flags == -1) {
            //     syslog(LOG_ERR, "client_to_main_pipe_fds[1] is invalid: errno=%d (%s)", errno, strerror(errno));
            // } else {
            //     syslog(LOG_INFO, "client_to_main_pipe_fds[1] is valid. Flags: %d", flags);
            // }
            ////test

            ssize_t wlen = write(client_to_main_pipe_fds[1], add_mesg, strlen(add_mesg)+1);
            //syslog(LOG_INFO,"Received Client data : %s",add_mesg);
            if(wlen <= 0) //클라이언트에게 받은걸 부모에게 쓴다.
                syslog(LOG_ERR,"cannot Write to parent");
            else{
                kill(main_server_pid,SIG_FROM_CLIENT);
                syslog(LOG_INFO,"after Write data to Main : %s",mesg);
            }
        } else if (n == -1 || (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 데이터가 아직 없음 (논블로킹 모드)
            //syslog(LOG_ERR, "Error reading client message");
            usleep(50000);
            continue;
        }
        else{
            syslog(LOG_ERR,"cannot read client message");
            exit(1);
        }
        //부모의 메시지를 읽는다 (여기가 채팅방 서버가 준 메시지)
        client_n = read(main_to_client_pipe_fds[0],mesg,BUFSIZ);
        if(client_n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            syslog(LOG_ERR, "continue first if");
            continue;
        }
        else if(client_n > 0){
            mesg[client_n] = '\0';
            syslog(LOG_INFO,"Received Chatroom data : %s",mesg);
            if(write(main_to_client_pipe_fds[1],mesg,client_n) <= 0) //부모의 메시지를
                syslog(LOG_ERR,"cannot Write to client");
            
        } else{
            syslog(LOG_ERR,"cannot read SERVER message");
            break;
        }
    }
    close(client_sock_fd); //할일 다 했으니까 종료
    //열어놓은 파이프 닫기
    close(main_to_client_pipe_fds[0]);
    close(client_to_main_pipe_fds[1]); 
}
