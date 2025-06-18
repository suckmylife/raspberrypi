#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static void *clnt_connection(void *arg);
int sendData(FILE* fp, char *ct, char *filename);
void sendOk(FILE* fp);
void sendError(FILE* fp);

int main(int argc, char **argv)
{
    int ssock;
    pthread_t thread;
    struct sockaddr_in servaddr, cliaddr;
    unsigned int len;

    if(argc != 2){
        printf("usage : %s <port>\n",argv[0]);
        return -1;
    }

    ssock = socket(AF_INET, SOCK_STREAM,0);
    if(ssock == -1){
        perror("socket()");
        return -1;
    }

    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = (argc != 2)?htons(8000) : htons(atoi(argv[1]));
    if(bind(ssock,(struct sockaddr *)&servaddr,sizeof(servaddr)) == -1){
        perror("bind()");
        return -1;
    }
    if(listen(ssock,10) == -1){
        perror("listen()");
        return -1;
    }

    while(1){
        char mesg[BUFSIZ];
        int csock;
        len = sizeof(cliaddr);
        csock = accept(ssock,(struct sockaddr *)&cliaddr,&len);

        inet_ntop(AF_INET, &cliaddr.sin_addr, mesg,BUFSIZ);
        printf("Client IP : %s:%d\n",mesg,ntohs(cliaddr.sin_port));

        pthread_create(&thread,NULL,clnt_connection,&csock);
        pthread_join(thread,NULL);
    }
    return 0;
}

void *clnt_connection(void *arg)
{
    int csock = *((int*)arg);
    FILE *clnt_read, *clnt_write;
    char reg_line[BUFSIZ], reg_buf[BUFSIZ];
    char method[BUFSIZ], type[BUFSIZ];
    char filename[BUFSIZ], *ret;

    clnt_read = fdopen(csock,"r");
    clnt_write = fdopen(dup(csock),"w");

    fgets(reg_line, BUFSIZ, clnt_read);
    fputs(reg_line,stdout);

    ret=strtok(reg_line,"/ ");
    strcpy(method,(ret != NULL)?ret:"");
    if(strcmp(method,"POST") == 0){
        sendOk(clnt_write);
        goto END;
    }
    else if(strcmp(method,"GET") != 0){
        sendError(clnt_write);
        goto END;
    }

    ret =strtok(NULL," ");
    strcpy(filename,(ret != NULL)?ret:"");
    if(filename[0] == '/'){
        for(int i = 0, j=0; i<BUFSIZ; i++,j++){
            if(filename[0] == '/')j++;
            filename[i] = filename[j];
            if(filename[j] == '\0') break;
        }
    }

    do{
        fgets(reg_line,BUFSIZ, clnt_read);
        fputs(reg_line, stdout);
        strcpy(reg_buf,reg_line);
        char *str = strchr(reg_buf, ':');
    }while(strncmp(reg_line, "\r\n", 2));

    sendData(clnt_write,type,filename);
END:
    fclose(clnt_read);
    fclose(clnt_write);
    pthread_exit(0);
    return (void *)NULL;
}

int sendData(FILE* fp, char *ct, char *filename)
{
    char protocol[] = "HTTP/1.1 200 OK \r\n";
    char server[] = "Sever: MY SEVER \r\n";
    char cnt_type[] = "Content-Type:text/html\r\n";
    char end[] = "\r\n";
    char buf[BUFSIZ];
    int fd, len;

    fputs(protocol, fp);
    fputs(server, fp);
    fputs(cnt_type,fp);
    fputs(end, fp);

    fd = open(filename,O_RDWR);
    do{
        len = read(fd,buf,BUFSIZ);
        fputs(buf,fp);
    }while(len == BUFSIZ);
    close(fd);
    return 0;
}

void sendOk(FILE* fp)
{
    char protocol[] = "HTTP/1.1 200 OK \r\n";
    char server[] = "Sever: MY SEVER \r\n\r\n";
    fputs(protocol, fp);
    fputs(server, fp);
    fflush(fp);
}

void sendError(FILE* fp)
{
    char protocol[] = "HTTP/1.1 200 OK \r\n";
    char server[] = "Sever: MY SEVER \r\n";
    char cnt_len[] = "Content-Length : 1024 \r\n";
    char cnt_type[] = "Content-Type:text/html\r\n\r\n";

    char content1[] = "<html><head><title>BAD Connection</title></head>";
    char content2[] = "<body><font size:+5>BAD Request</font></body></html>";

    printf("send error \n");
    fputs(protocol, fp);
    fputs(server, fp);
    fputs(cnt_len,fp);
    fputs(cnt_type, fp);
    fputs(content1, fp);
    fputs(content2, fp);
    fflush(fp);
}