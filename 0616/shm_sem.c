#include <stdio.h>
#include <stdlib.h> 		  /* exit( ) 함수를 위해 사용 */
#include <unistd.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h> 		/* waitpid() 함수를 위해 사용 */

#define SHM_KEY 0x12345 	/* 공유 메모리를 위한 키 */
#define SEM_KEY 0x12346   /* 공유 메모리를 위한 키 */

static union semun arg;   /* semun 공용체 */
static int semid;

static void p(int n) 		  /* 세마포어의 P 연산 */
{
    struct sembuf pbuf;
    pbuf.sem_num = 0;
    pbuf.sem_op = -n;
    pbuf.sem_flg = SEM_UNDO;

    if(semop(semid, &pbuf, 1) == -1) 	  /* 세마포어의 감소 연산을 수행한다. */
        perror("p : semop()");
}

static void v(int n) 		  /* 세마포어의 V 연산 */
{
    struct sembuf vbuf;
    vbuf.sem_num = 0;
    vbuf.sem_op = n;
    vbuf.sem_flg = SEM_UNDO;

    if(semop(semid, &vbuf, 1) == -1) 	  /* 세마포어의 증가 연산을 수행한다. */
        perror("v : semop()");
}

int main(int argc, char **argv)
{
    int i, pid, shmid;
    int *cVal;
    void *shmmem = (void *)0;
    int status, sem_val;
    union semun { /* semun 공용체 */
        int val;
        struct semid_ds *buf;
        unsigned short int *arrary;
    } arg;

    /* 세마포어에 대한 채널 얻기 */
    if((semid = semget(SEM_KEY, 1, IPC_CREAT | 0666)) == -1) {
        perror("semget()");
        return -1;
    }

    arg.val = 0; 			    /* 세마포어 값을 1로 설정 */
    if(semctl(semid, 0, SETVAL, arg) == -1) {
        perror("semctl() : SETVAL");
        return -1;
    }

    if((pid = fork()) == 0) { 		/* 자식 프로세스를 위한 설정 */
        /* 공유 메모리 공간을 만든다. */
        /* 공유 메모리 공간을 가져온다. */
        shmid = shmget((key_t)SHM_KEY, sizeof(int), 0);
        if(shmid == -1) {
            perror("shmget()");
            return -1;
        }

        /* 공유 메모리를 사용하기 위해 프로세스의 메모리에 붙인다. */
        shmmem = shmat(shmid, (void *)0, SHM_RND);
        if(shmmem == (void *)-1) {
            perror("shmat()");
            return -1;
        }

        cVal = (int *)shmmem;
        *cVal = 1;
        for(i = 0; i < 3; i++) {
            *cVal += 1;
            printf("Child(%d) : %d\n", i, *cVal);
            p(1);
            usleep(100000);
            //int sem_val = semctl(semid, 0, GETVAL, arg);
            //printf("P op : %d\n", sem_val);
        }
    } else if(pid > 0) { 		/* 부모 프로세스로 공유 메모리의 내용을 표시 */
        /* 공유 메모리 공간을 만든다. */
        shmid = shmget((key_t)SHM_KEY, sizeof(int), 0666 | IPC_CREAT);
        if(shmid == -1) {
            perror("shmget()");
            return -1;
        }

        /* 공유 메모리를 사용하기 위해 프로세스의 메모리에 붙인다. */
        shmmem = shmat(shmid, (void *)0, 0);
        if(shmmem == (void *)-1) {
            perror("shmat()");
            return -1;
        }

        cVal = (int *)shmmem;
        for(i = 0; i < 3; i++) {
            sleep(1);
            v(1);
            printf("Parent(%d) : %d\n", i, *cVal);
            //sem_val = semctl(semid, 0, GETVAL, arg);
            //printf("V op : %d\n", sem_val);
        }

        waitpid(pid, &status, 0); 		/* 자식 프로세스의 종료를 기다리기 */
    }

    shmctl(shmid, IPC_RMID, 0);
    semctl(semid, 0, IPC_RMID, arg);

    return 0;
}