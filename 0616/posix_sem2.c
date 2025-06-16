#include <stdio.h> 					/* printf( ) 함수를 위해 사용 */
#include <fcntl.h>					/* 파일 플래그 상수를 위해 사용 */
#include <unistd.h>
#include <signal.h>					/* signal( ) 함수를 위해 사용 */
#include <string.h>
#include <semaphore.h>				/* POSIX 세마포어를 위한 헤더파일 */

static sem_t *sem; 					/* 세마포어를 위한 전역 변수 */
static int cnt = 0;					/* 세마포어에서 사용할 임계 구역 변수 */

static void p(void)					/* 세마포어의 P 연산 */
{
  sem_wait(sem);
  printf("p : %d\n", ++cnt);		/* 전역 변수 cnt 값을 증가 */
}

static void v(void) 		  		/* 세마포어의 V 연산 */
{
  sem_post(sem);
  printf("v : %d\n", --cnt);		/* 전역 변수 cnt 값을 감소 */
}

static void sigHandler(int signo)	/* 시그널 번호를 인자로 받아 처리. */
{
  if(signo == SIGINT) { 			/* Ctrl + C 키를 눌렀을 때 처리 */
    v( );
  } else if (signo == SIGTSTP) {	/* Ctrl + Z 키를 눌렀을 때 처리 */
    p( );
  } else {
    fprintf(stderr, "Catched signal : %s\n", strsignal(signo));
  }
}

int main(int argc, char **argv)
{
  const char* name = "posix_sem";
  unsigned int value = 8; 			/* 세마포어의 값 */

  /* SIGINT의 처리를 위한 핸들러 등록 */
  if(signal(SIGINT, sigHandler) == SIG_ERR) { 		
    perror("signal( ) : SIGINT");
    return -1;
  }
  signal(SIGTSTP, sigHandler);

  /* 세마포어 열기 */
  sem = sem_open(name, O_CREAT, S_IRUSR | S_IWUSR, value);

  p( );
  for(int i = 0; cnt != 0; i++) {
    printf("\r%d", i); 				/* 현재 줄에 덮어쓰면서 출력 */
    fflush(NULL); 					/* 개행 문자가 없으므로 바로 출력하지 않기 때문에 강제로 출력 */
    usleep(1000000);
  }

  /* 다 쓴 세마포어 닫고 정리 */
  sem_close(sem);
  printf("sem_destroy return value : %d\n", sem_destroy(sem));

  /* 세마포어 삭제 */
  sem_unlink(name);

  return 0;
}