#include <stdio.h>
#include <unistd.h>
#include <stdlib.h> /* exit( ) 함수를 위해 사용 */
#include <string.h> /* strsignal() 함수를 위해 사용 */
#include <signal.h> /* signal( ) 함수를 위해 사용 */
#include <sys/sem.h>/* 세마포어를 위한 헤더파일 */

static int cnt = 0; 
static int semid;
static void sigHandler(int);/* 시그널 처리용 핸들러 */

void p() /* 세마포어의 P 연산 */
{
  struct sembuf pbuf;
  pbuf.sem_num = 0;
  pbuf.sem_op = -1;
  pbuf.sem_flg = SEM_UNDO;

  if(semop(semid, &pbuf, 1) == -1)/* 세마포어의 감소 연산을 수행한다. */
    perror("p : semop()");
  else
    printf("p : %d\n", ++cnt); 
}

void v() /* 세마포어의 V 연산 */
{
  struct sembuf vbuf;
  vbuf.sem_num = 0;
  vbuf.sem_op = 1;
  vbuf.sem_flg = SEM_UNDO;     

  if(semop(semid, &vbuf, 1) == -1)/* 세마포어의 증가 연산을 수행한다. */
    perror("v : semop()");
  else
    printf("v : %d\n", --cnt); 
}

static void sigHandler(int signo)/* 시그널 번호를 인자로 받는다. */
{
  if(signo == SIGINT) { /* SIGINT 시그널이 발생했을 때 처리 */
    v();
  } else if (signo == SIGTSTP) {
    p();
  } else {
    fprintf(stderr, "Catched signal : %s\n", strsignal(signo));
  }
}

int main(int argc, char **argv)
{
  union semun { /* semun 공용체 */
    int val;
    struct semid_ds *buf;
    unsigned short int *arrary;
  } arg;

  /* SIGINT의 처리를 위한 핸들러 등록 */
  if(signal(SIGINT, sigHandler) == SIG_ERR) { 
    perror("signal() : SIGINT");
    return -1;
  }
  signal(SIGTSTP, sigHandler);

  /* 세마포어에 대한 채널 얻기 */
  if((semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666)) == -1) {
    perror("semget()");
    return -1;
  }

  arg.val = 8; /* 세마포어 값을 1로 설정 */
  if(semctl(semid, 0, SETVAL, arg) == -1) {
    perror("semctl() : SETVAL");
    return -1;
  }

  p();
  while(cnt != 0) {
    usleep(100000);
  }

  /* 세마포어에 대한 채널 삭제 */
  if(semctl(semid, 0, IPC_RMID, arg) == -1) {
    perror("semctl() : IPC_RMID");
    return -1;
  }

  return 0;
}