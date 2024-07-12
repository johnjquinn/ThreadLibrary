// File:	worker.c

// List all group member's name: John Quinn
// username of iLab: jjq23
// iLab Server: ilab2

#include "worker.h"

queue *create_queue()
{
	queue *q = malloc(sizeof(queue));
	q->head = NULL;
	q->size = 0;
	return q;
}

int queue_push(queue *q, tcb *TCB)
{
	if (TCB == NULL)
		return 1;
	tcb_node *place = (tcb_node *)malloc(sizeof(tcb_node));
	place->data = TCB;
	place->next = NULL;
	q->size++;
	if (q->head == NULL)
		q->head = place;
	else
	{
		tcb_node *ptr = q->head;
		while (ptr->next != NULL)
			ptr = ptr->next;
		ptr->next = place;
	}
	return 0;
}

tcb *queue_pop(queue *q)
{
	if (q->head == NULL)
		return NULL;
	tcb_node *out = q->head;
	q->head = q->head->next;
	q->size--;
	return out->data;
}

tcb *queue_peek(queue *q)
{
	return q->head->data;
}

void handle_interrupts(int);
void mlfq_reset(int);
static void schedule(void);
static void sched_rr(void);
static void sched_mlfq(void);
// PART 1

queue *runqueue;
queue *blockqueue;
queue *multiqueue[MLFQ_LEVELS];
static ucontext_t *sctx; // scheduler context
static struct itimerval it_val;
static struct sigaction action;
static tcb *Main;
long long total_response_time = 0;
long long total_turnaround_time = 0;
long avg_response_time = 0;
long avg_turnaround_time = 0;
struct timespec arrival, firstrun, completion;
int used_workers = 0;
int used_priority = 0;
_Bool first = 1;
_Bool lock_in_place = 0;
_Bool reset = 0;
_Bool init = 0;
tcb *CURRENT;
worker_t joiner = 0;

//#ifdef MLFQ
int quanta[MLFQ_LEVELS];
struct itimerval reset_timer;
struct sigaction mlfqaction;
//#endif

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
	if (runqueue == NULL)
		runqueue = create_queue();
	if (first)
	{

		// set up main thread context
		Main = (tcb *)malloc(sizeof(tcb));
		Main->threadID = (worker_t *)0;
		Main->status = READY;
		Main->priority = 0;
		Main->qElapsed = 0;
		Main->tctx = (ucontext_t *)malloc(sizeof(ucontext_t));
		getcontext(Main->tctx);
		Main->tctx->uc_stack.ss_sp = malloc(STACK_SIZE);
		Main->tctx->uc_stack.ss_size = STACK_SIZE;
		Main->tctx->uc_stack.ss_flags = 0;
		Main->isMain = 1;
		Main->started = 1; // main thread already started
		queue_push(runqueue, Main);

		// set up scheduler context
		sctx = (ucontext_t *)malloc(sizeof(ucontext_t));
		if (getcontext(sctx) == -1)
		{
			perror("Scheduler context failed\n");
			exit(1);
		}
		sctx->uc_link = Main->tctx;
		sctx->uc_stack.ss_sp = malloc(STACK_SIZE);
		sctx->uc_stack.ss_size = STACK_SIZE;
		sctx->uc_stack.ss_flags = 0;
		makecontext(sctx, (void (*)(void))schedule, 0);

		// set up signal handler
		action.sa_flags = 0;
		sigemptyset(&action.sa_mask);
		action.sa_handler = handle_interrupts;
		sigaction(SIGPROF, &action, NULL);

		// set up quanta timer
		it_val.it_value.tv_sec = QUEUE_TV_SEC(QUANTUM);
		it_val.it_value.tv_usec = QUEUE_TV_USEC(QUANTUM);
		it_val.it_interval.tv_sec = 0;
		it_val.it_interval.tv_usec = 0;

#ifdef MLFQ
		sigemptyset(&mlfqaction.sa_mask);
		mlfqaction.sa_flags - 0;
		mlfqaction.sa_handler = mlfq_reset;
		sigaction(SIGALRM, &mlfqaction, NULL);

		reset_timer.it_value.tv_sec = QUEUE_TV_SEC(MLFQ_RESET);
		reset_timer.it_value.tv_usec = QUEUE_TV_USEC(MLFQ_RESET);
		reset_timer.it_interval = reset_timer.it_value;
#endif

		// complete setup flags
		first = 0;
		init = 1;
	}
	// set up current thread
	tcb *new_tcb = (tcb *)malloc(sizeof(tcb));
	new_tcb->threadID = thread;
	new_tcb->status = READY;
	new_tcb->qElapsed = 0;
	new_tcb->priority = 0; // the lower the number, the higher the priority
	new_tcb->isMain = 0;
	new_tcb->started = 0;
	new_tcb->tctx = (ucontext_t *)malloc(sizeof(ucontext_t));
	new_tcb->arrival_time = (struct timespec *)malloc(sizeof(struct timespec));
	new_tcb->completion_time = (struct timespec *)malloc(sizeof(struct timespec));
	new_tcb->firstrun_time = (struct timespec *)malloc(sizeof(struct timespec));
	clock_gettime(CLOCK_REALTIME, new_tcb->arrival_time);
	getcontext(new_tcb->tctx);
	new_tcb->tctx->uc_link = sctx;
	new_tcb->tctx->uc_stack.ss_sp = malloc(STACK_SIZE);
	new_tcb->tctx->uc_stack.ss_size = STACK_SIZE;
	new_tcb->tctx->uc_stack.ss_flags = 0;
	makecontext(new_tcb->tctx, (void (*)(void))function, 1, arg);
	used_workers++;
	queue_push(runqueue, new_tcb);
	swapcontext(Main->tctx, sctx);
	return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{
	// rule 4b of MLFQ, yielded threads keep their priority
	struct itimerval pause = {0};
	setitimer(ITIMER_PROF, &pause, &it_val);
	CURRENT->status = YIELDED;
	queue_push(runqueue, CURRENT);
	swapcontext(CURRENT->tctx, sctx);
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr)
{
	if (value_ptr)
	{
		value_ptr = CURRENT->ret_val;
	}
	clock_gettime(CLOCK_REALTIME, CURRENT->completion_time);
	total_turnaround_time += (CURRENT->completion_time->tv_sec - CURRENT->arrival_time->tv_sec) * 1000000 + (CURRENT->completion_time->tv_nsec - CURRENT->arrival_time->tv_nsec);
	avg_turnaround_time = total_turnaround_time / used_workers;
	CURRENT->status = KILLED;
	swapcontext(CURRENT->tctx, sctx);
};

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr)
{
	if (CURRENT == NULL)
	{
		return 1;
	}
	joiner = 1;
	if (CURRENT->threadID != &thread || CURRENT->isMain)
	{
		worker_yield();
	}
	while (CURRENT->status != KILLED)
	{
		worker_yield();
	}
	if (value_ptr)
	{
		value_ptr[0] = CURRENT->ret_val;
	}
	CURRENT->status = KILLED;
	swapcontext(CURRENT->tctx, sctx);
	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
	if (mutex == NULL)
	{
		perror("mutex is null\n");
		exit(1);
	}
	//- initialize data structures for this mutex
	mutex->thread = 0; // default value
	mutex->init = 1;
	mutex->locked = 0;
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
	if (mutex == NULL)
	{
		perror("mutex is null\n");
		exit(1);
	}
	if (!mutex->init)
	{
		perror("mutex not initialized\n");
		exit(1);
	}
	if (!mutex->locked)
	{
		mutex->locked = 1;
		if (mutex->thread == 0)
		{
			mutex->thread = CURRENT->threadID;
		}
		lock_in_place = 1;
		return 0;
	}
	else
	{
		if (mutex->thread != CURRENT->threadID)
		{
			CURRENT->status = BLOCKED;
			worker_yield();
		}
	}
	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
	if (mutex == NULL)
	{
		perror("mutex is null\n");
		exit(1);
	}
	if (!mutex->init)
	{
		perror("mutex not initialized\n");
		exit(1);
	}
	if (!mutex->locked)
		return 0;
	else
	{
		if (mutex->thread != CURRENT->threadID)
		{
			worker_yield();
		}
		mutex->locked = 0;
		mutex->thread = 0;
		lock_in_place = 0;
	}
	return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
	if (mutex == NULL)
	{
		perror("mutex is null\n");
		exit(1);
	}
	if (mutex->locked)
	{
		perror("Destroying a locked mutex\n");
		exit(1);
	}
	mutex->init = 0;
	return 0;
};

// PART 2

void handle_interrupts(int signum)
{
	if (CURRENT == NULL)
		return;
	CURRENT->qElapsed++;
	// rule 4 of MLFQ
	if (CURRENT->priority < MLFQ_LEVELS - 1)
		CURRENT->priority++;
	CURRENT->status = READY;
	queue_push(runqueue, CURRENT);
	swapcontext(CURRENT->tctx, sctx);
}

void mlfq_reset(int signum)
{
	reset = 1;
	handle_interrupts(signum);
}

/* scheduler */
static void schedule()
{

#ifndef MLFQ
	sched_rr();
#else
	sched_mlfq();
#endif
}

/* Round-robin (RR) scheduling algorithm */
static void sched_rr()
{
	while (1)
	{
		CURRENT = queue_pop(runqueue);
		if (CURRENT == NULL)
			break;
		// main cannot join

		if (CURRENT->isMain && runqueue->head == NULL)
		{
			swapcontext(sctx, CURRENT->tctx);
		}
		if (CURRENT->status == RUNNING)
		{
			// likely shouldnt be here
			swapcontext(sctx, CURRENT->tctx);
			continue;
		}

		if (CURRENT->status == READY)
		{
			// thread is running for the first time
			if (!CURRENT->isMain && !CURRENT->started)
			{
				clock_gettime(CLOCK_REALTIME, CURRENT->firstrun_time);
				CURRENT->started = 1;
				total_response_time = (CURRENT->firstrun_time->tv_sec - CURRENT->arrival_time->tv_sec) * 1000000 + (CURRENT->firstrun_time->tv_nsec - CURRENT->arrival_time->tv_nsec);
				avg_response_time = total_response_time / used_workers;
			}

			setitimer(ITIMER_PROF, &it_val, NULL);
			CURRENT->status = RUNNING;
			swapcontext(sctx, CURRENT->tctx);
			continue;
		}

		if (CURRENT->status == YIELDED)
		{
			// gave up control to the cpu before
			it_val.it_value.tv_sec = QUEUE_TV_SEC(QUANTUM);
			it_val.it_value.tv_usec = QUEUE_TV_USEC(QUANTUM);
			it_val.it_interval.tv_sec = 0;
			it_val.it_interval.tv_usec = 0;
			setitimer(ITIMER_PROF, &it_val, NULL);
			CURRENT->status = RUNNING;
			swapcontext(sctx, CURRENT->tctx);
			continue;
		}

		if (CURRENT->status == KILLED)
		{
			if (CURRENT->isMain)
			{
				perror("Main thread killed\n");
				exit(1);
			}
			// thread is done and will be removed for good
			free(CURRENT);
			continue;
		}

		if (CURRENT->status == BLOCKED)
		{
			// mutex is active?
			if (lock_in_place)
			{
				queue_push(runqueue, CURRENT);
				continue;
			}
			// mutex unlocked
			it_val.it_value.tv_sec = QUEUE_TV_SEC(QUANTUM);
			it_val.it_value.tv_usec = QUEUE_TV_USEC(QUANTUM);
			it_val.it_interval.tv_sec = 0;
			it_val.it_interval.tv_usec = 0;
			setitimer(ITIMER_PROF, &it_val, NULL);
			CURRENT->status = RUNNING;
			swapcontext(sctx, CURRENT->tctx);
			continue;
		}
	}
	printf("Average turnaround time: %ld\n", avg_turnaround_time);
	printf("Average response time: %ld\n", avg_response_time);
	setcontext(Main->tctx);
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq()
{
	// fix implementation
	if (init)
	{
		multiqueue[0] = create_queue();
		for (int i = 1; i < MLFQ_LEVELS; i++)
		{
			multiqueue[i] = create_queue();
		}
		init = 0;
	}
	while (1)
	{
		// all old jobs going to top priority again
		if (reset)
		{
			for (int i = 1; i < MLFQ_LEVELS; i++)
			{
				tcb *ptr;
				while (multiqueue[i]->head != NULL)
				{
					ptr = queue_pop(multiqueue[i]);
					ptr->priority = 0;
					queue_push(runqueue, ptr);
				}
			}
			used_priority = 0;
			reset = 0;
		}

		// everything is finished
		if (CURRENT == NULL && runqueue->head == NULL)
			break;

		setitimer(ITIMER_REAL, &reset_timer, NULL);
		CURRENT = queue_pop(runqueue);
		// current thread is on lower priority
		if (CURRENT == NULL)
		{
			for (int i = 0; i < MLFQ_LEVELS; i++)
			{
				if (multiqueue[i]->head != NULL)
				{
					queue_push(runqueue, queue_pop(multiqueue[i]));
					used_priority = i;
					break;
				}
			}
			if (used_priority == 0)
			{
				break;
			}
			continue;
		}

		// actually run the current threads
		if (CURRENT->priority < used_priority)
		{
			if (used_priority == 0)
			{
				perror("Used priority not set\n");
				exit(1);
			}
			else
				swapcontext(sctx, CURRENT->tctx);
		}

		else if (CURRENT->priority == used_priority)
		{
			// essentially rr
			if (CURRENT->isMain && runqueue->head == NULL)
			{
				swapcontext(sctx, CURRENT->tctx);
			}
			if (CURRENT->status == RUNNING)
			{
				// likely shouldnt be here
				swapcontext(sctx, CURRENT->tctx);
				continue;
			}

			if (CURRENT->status == READY)
			{
				if (!CURRENT->isMain && !CURRENT->started)
				{
					clock_gettime(CLOCK_REALTIME, CURRENT->firstrun_time);
					CURRENT->started = 1;
				}
				setitimer(ITIMER_PROF, &it_val, NULL);
				CURRENT->status = RUNNING;
				swapcontext(sctx, CURRENT->tctx);
				continue;
			}

			if (CURRENT->status == YIELDED)
			{
				// gave up control to the cpu before
				it_val.it_value.tv_sec = QUEUE_TV_SEC(QUANTUM);
				it_val.it_value.tv_usec = QUEUE_TV_USEC(QUANTUM);
				it_val.it_interval.tv_sec = 0;
				it_val.it_interval.tv_usec = 0;
				setitimer(ITIMER_PROF, &it_val, NULL);
				CURRENT->status = RUNNING;
				swapcontext(sctx, CURRENT->tctx);
				continue;
			}

			if (CURRENT->status == KILLED)
			{
				if (CURRENT->isMain)
				{
					perror("Main thread killed\n");
					exit(1);
				}
				// thread is done and will be removed for good
				free(CURRENT);
				continue;
			}

			if (CURRENT->status == BLOCKED)
			{
				// mutex is active?
				if (lock_in_place)
				{
					queue_push(runqueue, CURRENT);
					continue;
				}
				// mutex unlocked
				it_val.it_value.tv_sec = QUEUE_TV_SEC(QUANTUM);
				it_val.it_value.tv_usec = QUEUE_TV_USEC(QUANTUM);
				it_val.it_interval.tv_sec = 0;
				it_val.it_interval.tv_usec = 0;
				setitimer(ITIMER_PROF, &it_val, NULL);
				CURRENT->status = RUNNING;
				swapcontext(sctx, CURRENT->tctx);
				continue;
			}
		}
		else
		{
			// needs to be put in lower priority queue and yield to other queue
			if (CURRENT->priority+1 >= MLFQ_LEVELS)
			{
				break;
			}
			else
			{
				queue_push(multiqueue[CURRENT->priority + 1], CURRENT);
				continue;
			}
		}
	}
	setcontext(Main->tctx);
}
