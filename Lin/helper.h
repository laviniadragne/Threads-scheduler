#include "so_scheduler.h"

#include <pthread.h>
#include <semaphore.h>

#define NEW_STATE 1
#define READY_STATE 2
#define RUNNING_STATE 3
#define WAITING_STATE 4
#define TERMINATED_STATE 5
#define CODE_ERR -1
#define SUCCESS 0
#define MAX_IO 256
#define MAX_THREADS 1000
#define INITIAL_THREAD -1
#define FIRST_THREAD_ID 0
#define UNINITIALIZED 0
#define INITIALIZED 1
#define MAX_PRIO 5
#define COUNT_SEM 0

typedef struct thread {
	unsigned int priority;
	int state;
	so_handler *func;
	int id;
	unsigned int actual_quantum;
} thread;

typedef struct scheduler {
	thread *threads;
	pthread_t *real_threads;
	thread *prio_queue;
	int queue_size;
	thread curr_thread;
	int id_curr_thread;
	unsigned int time_quantum;
	unsigned int io;
	int total_threads;
} scheduler;

static scheduler *so_scheduler;
static sem_t *sems;
static int init_scheduler;
static pthread_mutex_t mutex;
static sem_t sem_terminated;
static thread **waiting_threads;
static int *waiting_size;

void add_in_queue(thread new_thread);

void delete_from_queue(void);

void *start_thread(void *args);

int switch_threads(void);

int schedule(void);

int curr_comes_from_queue(void);
