// File:	worker_t.h

// List all group member's name: John Quinn
// username of iLab: jjq23
// iLab Server: ilab2

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

#define MLFQ_LEVELS 4
#define MLFQ_RESET 1000
#define QUANTUM 20

/* include lib header files that you need here: */
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

#define QUEUE_SIZE SIGSTKSZ
#define STACK_SIZE SIGSTKSZ

#define QUEUE_TV_SEC(interval) interval / 1000
#define QUEUE_TV_USEC(interval) (interval * 1000) % 1000000 

enum STATUS
{
	READY,
	RUNNING,
	BLOCKED,
	KILLED,
	YIELDED
};

typedef uint worker_t;

typedef struct TCB
{
	/* add important states in a thread control block */
	// thread context
	// thread stack
	// And more ...

	int status;
	int priority;
	worker_t* threadID;
	ucontext_t* tctx;
	int qElapsed; // number of quanta elapsed since beginning run
	struct timespec* arrival_time;
	struct timespec* firstrun_time;
	struct timespec* completion_time;
	void * ret_val; //return value
	_Bool isMain;
	_Bool started;
} tcb;

/* mutex struct definition */
typedef struct worker_mutex_t
{
	worker_t* thread;
	_Bool init;
	_Bool locked;
} worker_mutex_t;

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

typedef struct tcb_node
{
	tcb *data;
	struct tcb_node *next;
} tcb_node;

// queue structure
typedef struct queue
{
	tcb_node *head;
	int size;
} queue;

queue *create_queue();
int queue_push(queue *, tcb *);
tcb *queue_pop(queue *);
tcb *queue_peek(queue *);

/* Function Declarations: */

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
												 *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);


#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif
