#include "helper.h"

DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io)
{
	int i;

	if (io > MAX_IO || time_quantum == 0)
		return CODE_ERR;

	if (init_scheduler == UNINITIALIZED) {
		mutex = CreateMutex(NULL, FALSE, NULL);
		if (mutex == NULL)
			return CODE_ERR;

		/* Aloc memorie pentru scheduler */
		so_scheduler = malloc(sizeof(scheduler));
		if (so_scheduler == NULL)
			return CODE_ERR;

		so_scheduler->io = io;
		so_scheduler->time_quantum = time_quantum;
		so_scheduler->queue_size = 0;
		so_scheduler->total_threads = 0;
		so_scheduler->id_curr_thread = INITIAL_THREAD;
		so_scheduler->threads = malloc(sizeof(thread) * MAX_THREADS);
		if (so_scheduler->threads == NULL) {
			free(so_scheduler);
			return CODE_ERR;
		}
		so_scheduler->real_threads =
		    (HANDLE *)malloc(MAX_THREADS * sizeof(HANDLE));
		if (so_scheduler->threads == NULL) {
			free(so_scheduler->threads);
			free(so_scheduler);
			return CODE_ERR;
		}
		so_scheduler->prio_queue = malloc(sizeof(thread) * MAX_THREADS);
		if (so_scheduler->prio_queue == NULL) {
			free(so_scheduler->threads);
			free(so_scheduler->real_threads);
			free(so_scheduler);
			return CODE_ERR;
		}

		sems = malloc(sizeof(HANDLE) * MAX_THREADS);
		if (sems == NULL) {
			free(so_scheduler->threads);
			free(so_scheduler->real_threads);
			free(so_scheduler->prio_queue);
			free(so_scheduler);
			return CODE_ERR;
		}

		waiting_size = calloc(MAX_IO + 1, sizeof(int));
		if (waiting_size == NULL) {
			free(so_scheduler->threads);
			free(so_scheduler->real_threads);
			free(so_scheduler->prio_queue);
			free(so_scheduler);
			free(sems);
		}

		waiting_threads = calloc(sizeof(thread *), MAX_IO + 1);
		if (waiting_threads == NULL) {
			free(sems);
			free(so_scheduler->threads);
			free(so_scheduler->real_threads);
			free(so_scheduler->prio_queue);
			free(so_scheduler);
			free(waiting_size);
		}

		for (i = 0; i < MAX_IO; i++) {
			waiting_threads[i] =
			    calloc(sizeof(thread), MAX_THREADS);
			if (waiting_threads[i] == NULL) {
				free(sems);
				free(waiting_threads);
				free(so_scheduler->threads);
				free(so_scheduler->real_threads);
				free(so_scheduler->prio_queue);
				free(so_scheduler);
				free(waiting_size);
			}
		}

		init_scheduler = INITIALIZED;
		return SUCCESS;
	}

	return CODE_ERR;
}

/* Adauga un element in coada */
void add_in_queue(thread new_thread)
{
	int i = 0, j;

	for (i = 0; i < so_scheduler->queue_size; i++)
		/* Trebuie inserat inainte de i, deci pe pozitia i - 1 */
		/* Daca prioritatile sunt egale, se trece la urmatorul */
		if (so_scheduler->prio_queue[i].priority < new_thread.priority)
			break;

	/* De la pozitia i pana la final trebuie mutate */
	/* elementele cu 1 pozitie */
	for (j = so_scheduler->queue_size; j >= i + 1; j--)
		so_scheduler->prio_queue[j] = so_scheduler->prio_queue[j - 1];

	so_scheduler->prio_queue[i] = new_thread;

	so_scheduler->queue_size++;
}

/* Functia in care se determina contextul pentru */
/* executarea unui thread */
void *start_thread(void *args)
{
	int err;
	DWORD err_d;
	thread *th = (thread *)args;

	/* Asteapta sa fie planificat */
	err_d = WaitForSingleObject(sems[th->id], INFINITE);
	if (err_d == WAIT_FAILED)
		return NULL;

	/* Asteapta sa fie planificat */
	th->func(th->priority);

	th->state = TERMINATED_STATE;
	so_scheduler->curr_thread.state = TERMINATED_STATE;

	/* Se face replanificare */
	err = schedule();
	if (err != SUCCESS)
		return NULL;

	return NULL;
}

/* Sterge primul element din coada */
void delete_from_queue(void)
{
	int i;

	for (i = 0; i < so_scheduler->queue_size - 1; i++)
		so_scheduler->prio_queue[i] = so_scheduler->prio_queue[i + 1];

	so_scheduler->queue_size--;
}

/* Opreste thread-ul curent din rulare si il */
/* porneste pe primul din coada */
int switch_threads(void)
{
	int err;
	thread th_last, th_curr;

	th_curr = so_scheduler->prio_queue[0];
	th_last = so_scheduler->curr_thread;

	/* Opresc curentul si il pornesc pe cel din coada */
	so_scheduler->curr_thread = so_scheduler->prio_queue[0];
	so_scheduler->id_curr_thread = so_scheduler->prio_queue[0].id;
	so_scheduler->curr_thread.actual_quantum = so_scheduler->time_quantum;
	so_scheduler->curr_thread.state = RUNNING_STATE;
	th_last.actual_quantum = so_scheduler->time_quantum;

	/* Elimin din coada primul element si adaug curentul */
	delete_from_queue();
	add_in_queue(th_last);

	err = ReleaseSemaphore(sems[th_curr.id], 1, 0);
	if (err == 0)
		return CODE_ERR;

	return SUCCESS;
}

/* Setez ca si thread curent in executie */
/* primul din coada */
int curr_comes_from_queue(void)
{
	int err;

	/* Il iau pe primul din coada */
	so_scheduler->curr_thread = so_scheduler->prio_queue[0];
	so_scheduler->id_curr_thread = so_scheduler->prio_queue[0].id;
	so_scheduler->curr_thread.actual_quantum = so_scheduler->time_quantum;
	so_scheduler->curr_thread.state = RUNNING_STATE;

	/* Elimin primul element din coada */
	delete_from_queue();
	/* Las thread-ul sa se execute */
	err = ReleaseSemaphore(sems[so_scheduler->id_curr_thread], 1, 0);
	if (err == 0)
		return CODE_ERR;

	return SUCCESS_WIN;
}

/* Planifica thread-ul care va rula */
int schedule(void)
{
	int err;

	/* Thread-ul curent a terminat si coada e goala */
	if (so_scheduler->id_curr_thread != INITIAL_THREAD)
		if (so_scheduler->curr_thread.state == TERMINATED_STATE &&
		    so_scheduler->queue_size == 0)
			return SUCCESS;

	/* Daca nu exista un thread care ruleaza la momentul asta */
	if (so_scheduler->id_curr_thread == INITIAL_THREAD) {
		/* Daca am ce sa aleg din coada */
		if (so_scheduler->queue_size != 0) {
			err = curr_comes_from_queue();
			if (err != SUCCESS_WIN)
				return err;
		}
	} else {
		if (so_scheduler->queue_size != 0) {
			/* Daca coada nu e goala si curentul e terminated */
			/* Aleg primul din coada */
			if (so_scheduler->curr_thread.state ==
			    TERMINATED_STATE ||
				so_scheduler->curr_thread.state ==
				WAITING_STATE) {
				err = curr_comes_from_queue();
				if (err != SUCCESS_WIN)
					return err;
			}

			/* Curentul nu a terminat, dar e un candidat mai */
			/* bun in coada, curentul face schimb cu el */
			else if (so_scheduler->prio_queue[0].priority >
				 so_scheduler->curr_thread.priority) {
				err = switch_threads();
				if (err != SUCCESS)
					return err;
			}

			/* Nu a terminat, dar a expirat cuanta si */
			/* e unul in coada cu aceeasi prioritate */
			else if (so_scheduler->curr_thread.actual_quantum <=
				 0) {
				if (so_scheduler->curr_thread.priority ==
				    so_scheduler->prio_queue[0].priority) {
					/* Switch la thread-uri */
					err = switch_threads();
					if (err != SUCCESS)
						return err;
				}

				/* Nu exista un candidat mai bun decat */
				/* actualul, trebuie programat tot */
				/* actualul, pentru ca i-a expirat */
				/* cuanta */
				else {
					so_scheduler->curr_thread
					    .actual_quantum =
					    so_scheduler->time_quantum;
					so_scheduler->curr_thread.state =
					    RUNNING_STATE;

					err = ReleaseSemaphore(
					    sems[so_scheduler->id_curr_thread],
					    1, 0);
					if (err == 0)
						return CODE_ERR;
				}
			}
			/* Nu a expirat cuanta, doar continua acelasi thread */
			else {
				so_scheduler->curr_thread.state = RUNNING_STATE;
				err = ReleaseSemaphore(
				    sems[so_scheduler->id_curr_thread], 1, 0);
				if (err == 0)
					return CODE_ERR;
			}
		} else {
			/* Nu exista un candidat mai bun decat actualul */
			/* si i-a expirat cuanta, trebuie programat tot */
			/* actualul */
			if (so_scheduler->curr_thread.actual_quantum <= 0) {
				so_scheduler->curr_thread.actual_quantum =
				    so_scheduler->time_quantum;
				so_scheduler->curr_thread.state = RUNNING_STATE;

				err = ReleaseSemaphore(
				    sems[so_scheduler->id_curr_thread], 1, 0);
				if (err == 0)
					return CODE_ERR;
			}

			/* Coada e goala, dar actualului nu i-a expirat */
			/* cuanta, continua tot el */
			else {
				so_scheduler->curr_thread.state = RUNNING_STATE;
				err = ReleaseSemaphore(
				    sems[so_scheduler->id_curr_thread], 1, 0);
				if (err == 0)
					return CODE_ERR;
			}
		}
	}

	return SUCCESS;
}

DECL_PREFIX tid_t so_fork(so_handler *func, unsigned int priority)
{
	int err, id_new_thread;
	DWORD thread_id, err_d;

	if (init_scheduler == UNINITIALIZED)
		return INVALID_TID;

	if (priority > MAX_PRIO)
		return INVALID_TID;

	if (func == NULL)
		return INVALID_TID;

	/* Initializez cu date thread-ul */
	so_scheduler->threads[so_scheduler->total_threads].priority = priority;
	so_scheduler->threads[so_scheduler->total_threads].id =
	    so_scheduler->total_threads;
	so_scheduler->threads[so_scheduler->total_threads].func = func;
	so_scheduler->threads[so_scheduler->total_threads].actual_quantum =
	    so_scheduler->time_quantum;
	so_scheduler->threads[so_scheduler->total_threads].state = READY_STATE;
	sems[so_scheduler->total_threads] = CreateSemaphore(0, 0, 1, 0);
	if (sems[so_scheduler->total_threads] == NULL)
		return INVALID_TID;

	id_new_thread = so_scheduler->total_threads;

	so_scheduler->real_threads[so_scheduler->total_threads] = CreateThread(
	    NULL, 0, (LPTHREAD_START_ROUTINE)start_thread,
	    &so_scheduler->threads[so_scheduler->total_threads], 0, &thread_id);
	if (so_scheduler->real_threads[so_scheduler->total_threads] == NULL)
		return INVALID_TID;

	/* Adaug in coada de prioritati thread-ul */
	err_d = WaitForSingleObject(mutex, INFINITE);
	if (err_d == WAIT_FAILED)
		return INVALID_TID;
	add_in_queue(so_scheduler->threads[so_scheduler->total_threads]);
	so_scheduler->total_threads++;
	err = ReleaseMutex(mutex);
	if (err == 0)
		return INVALID_TID;

	/* Fac planificare */
	so_exec();

	return thread_id;
}

DECL_PREFIX int so_wait(unsigned int io)
{
	if (io >= so_scheduler->io)
		return CODE_ERR;

	if (init_scheduler == UNINITIALIZED)
		return CODE_ERR;

	/* Adaug thread-ul in vectorul de asteptare */
	waiting_threads[io][waiting_size[io]] = so_scheduler->curr_thread;
	/* Maresc dimensiunea vectorului */
	waiting_size[io]++;
	so_scheduler->curr_thread.state = WAITING_STATE;

	so_exec();

	return SUCCESS;
}

DECL_PREFIX int so_signal(unsigned int io)
{
	int i, size;

	if (io >= so_scheduler->io)
		return CODE_ERR;

	/* Bag in coada tot ce era in waiting la acel io */
	for (i = 0; i < waiting_size[io]; i++) {
		waiting_threads[io][i].state = READY_STATE;
		add_in_queue(waiting_threads[io][i]);
		memset(&waiting_threads[io][i], 0, sizeof(thread));
	}
	size = waiting_size[io];
	waiting_size[io] = 0;

	so_exec();

	return size;
}

DECL_PREFIX void so_exec(void)
{
	int err;
	DWORD err_d;
	int id = so_scheduler->id_curr_thread;
	thread curr = so_scheduler->curr_thread;

	/* Scade cuanta de timp */
	/* Replanifica thread-ul, daca e cazul */
	so_scheduler->curr_thread.actual_quantum--;
	err = schedule();
	if (err != SUCCESS)
		return;

	/* Asteapta sa reintre in executie cel care */
	/* a fost curent pana acum */
	if (id != INITIAL_THREAD) {
		err_d = WaitForSingleObject(sems[curr.id], INFINITE);
		if (err_d == WAIT_FAILED)
			return;
	}
}

DECL_PREFIX void so_end(void)
{
	int i, err;
	DWORD err_d;

	if (so_scheduler == NULL)
		return;

	/* Astept terminarea tuturor thread-urilor */
	for (i = 0; i < so_scheduler->total_threads; i++) {
		err_d = WaitForSingleObject(so_scheduler->real_threads[i],
					    INFINITE);
		if (err_d == WAIT_FAILED)
			return;
	}

	for (i = 0; i < so_scheduler->total_threads; i++) {
		err = CloseHandle(sems[i]);
		if (err == 0)
			return;
	}

	for (i = 0; i < MAX_IO; i++)
		free(waiting_threads[i]);
	free(waiting_threads);

	free(waiting_size);
	free(so_scheduler->threads);
	free(so_scheduler->real_threads);
	free(so_scheduler->prio_queue);
	free(so_scheduler);
	free(sems);
	/* Destroy la mutex */
	err = CloseHandle(mutex);
	if (err == 0)
		return;
	init_scheduler = UNINITIALIZED;
}
