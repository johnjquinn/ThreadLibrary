#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <ucontext.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>

#define INTERVAL 500 /* number of milliseconds to go off */

/* function prototype */
void DoStuff(void);

int main(int argc, char *argv[])
{

    struct itimerval it_val; /* for setting itimer */

    /* Upon SIGALRM, call DoStuff().
     * Set interval timer.  We want frequency in ms,
     * but the setitimer call needs seconds and useconds. */
    if (signal(SIGALRM, (void (*)(int))DoStuff) == SIG_ERR)
    {
        printf("Unable to catch SIGALRM");
        exit(1);
    }
    it_val.it_value.tv_sec = INTERVAL / 1000;
    it_val.it_value.tv_usec = (INTERVAL * 1000) % 1000000;
    it_val.it_interval = it_val.it_value;
    if (setitimer(ITIMER_REAL, &it_val, NULL) == -1)
    {
        printf("error calling setitimer()");
        exit(1);
    }

    while (1)
        pause();
}

/*
 * DoStuff
 */
void DoStuff(void)
{

    printf("Timer went off.\n");
}
