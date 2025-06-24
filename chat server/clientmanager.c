#include "clientmanager.h"

void client_work(int client_sock_fd, int *main_to_client_pipe_fds, int *client_to_main_pipe_fds)
{
    pid_t main_pid, client_pid;
    char mesg[BUFSIZ]; //메시지 읽는거
    
    main_pid = getppid();
    client_pid = getpid();
    set_nonblocking(client_sock_fd);
    set_nonblocking(main_to_client_pipe_fds[0]);
    //부모는 parent_pfd[1]에 쓰고, 자식은 parent_pfd[0]에서 읽음
    //자식은 읽어야 하니까 반대를 닫는다
    close(main_to_client_pipe_fds[1]); 
    //자식은 child_pfd[1]에 쓰고, 부모는 child_pfd[0]에서 읽음
    //자식이 써야 하니까 반대를 닫는다
    close(client_to_main_pipe_fds[0]);
    while(!is_shutdown)
    {
        n=read(client_sock_fd,mesg,BUFSIZ-1)
        //클라이언트에서 메시지를 받는다
        if( n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            continue;
        }
        else if(n > 0){
            mesg[n] = '\0';
            syslog(LOG_INFO,"Received data : %s",mesg);
            if(write(client_to_main_pipe_fds[1],mesg,n) <= 0) //클라이언트에게 받은걸 부모에게 쓴다.
                syslog(LOG_ERR,"cannot Write to parent");
            else{
                kill(main_pid,SIGUSR2);
            }
        }
        else{
            syslog(LOG_ERR,"cannot read client message");
            break;
        }
        //부모의 메시지를 읽는다 (여기가 채팅방 서버가 준 메시지)
        client_n = read(main_to_client_pipe_fds[0],mesg,BUFSIZ);
        if(client_n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            continue;
        }
        else if(client_n > 0){
            mesg[client_n] = '\0';
            syslog(LOG_INFO,"Received data : %s",mesg);
            // if(write(csock,mesg,n) <= 0) //부모의 메시지를
            //     syslog(LOG_ERR,"cannot Write to client");
            
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