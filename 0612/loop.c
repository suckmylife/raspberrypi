#include <stdio.h>
#include <unistd.h>
#include <signal.h>

void sigHdl(int no)
{
	printf("%d\n",no);
}

int main(void)
{
	int i;
	signal(SIGINT,sigHdl);

	for(i = 0; ; i++){
		printf("%10d\r",i);
		fflush(NULL);
		sleep(1);
	}
	return 0;
}

