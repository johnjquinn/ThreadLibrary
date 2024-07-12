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

#define STACK_SIZE SIGSTKSZ
#define INTERVAL 1000
ucontext_t fctx, mctx, bctx;
int current;
void foo()
{
    while (1)
    {
        printf("foo\n");
        pause();
    }
}
void bar()
{
    while (1)
    {
        printf("bar\n");
        pause();
    }
}
void handler(int signum);
int main()
{
    current = 0;
    getcontext(&fctx);
    getcontext(&bctx);
    char *stack1 = malloc(STACK_SIZE);
    char *stack2 = malloc(STACK_SIZE);
    fctx.uc_link = NULL;
    fctx.uc_stack.ss_flags = 0;
    fctx.uc_stack.ss_sp = stack1;
    fctx.uc_stack.ss_size = STACK_SIZE;
    bctx.uc_link = NULL;
    bctx.uc_stack.ss_flags = 0;
    bctx.uc_stack.ss_sp = stack2;
    bctx.uc_stack.ss_size = STACK_SIZE;
    makecontext(&fctx, foo, 0);
    makecontext(&bctx, bar, 0);
    struct sigaction sa;
    struct itimerval timer;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handler;
    sigaction(SIGALRM, &sa, NULL);
    timer.it_value.tv_sec = INTERVAL / 1000;
    timer.it_value.tv_usec = (INTERVAL * 1000) % 1000000;
    timer.it_interval = timer.it_value;
    setitimer(ITIMER_REAL, &timer, NULL);
    setcontext(&fctx);
    while (1)
        pause();
}

void handler(int signum)
{
    if (current) // bar was running
    {
        current = 0;
        swapcontext(&bctx, &fctx);
    }
    else
    {
        current = 1;
        swapcontext(&fctx, &bctx);
    }
}