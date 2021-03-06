#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

#include "test.h"
char * req_status_string[]=
{
	"Initiated",
	"Setup",
	"Sent",
	"WIP",
	"DONE",
};
int req_number;
struct request *request_array;

void delay(void)
{
	usleep(1);
}

static void reqStatusWrong(int rold, int eold, int new)
{
	tprintf("old staus is %s while expected change from %s to %s\n",
		req_status_string[rold],
		req_status_string[eold],
		req_status_string[new]);
}

int logRequest(struct request *req)
{
	int oldstat;

	if (!req || req->status != reqs_done)
		return -EFAULT;

	req->stats.t_reqs += req->req.size;
	req->stats.t_missed += (req->req.size - req->done);
	req->stats.t_aborted += req->eabort;
	oldstat = __sync_val_compare_and_swap(&req->status, reqs_done, reqs_initial);
	if (oldstat != reqs_done)
	{
		reqStatusWrong(oldstat, reqs_done, reqs_initial);
		return -EFAULT;
	}

	return 0;
}

static inline void abort_request(struct request *req)
{
	req->eabort = 1;
}

static int hit_deadline(struct request *req)
{
	ttime_t now = getNow();

	if (now > req->deadline)
		return 1;
	else
		return 0;
}

/*
 * Wait till the queue is clear
 * sync:
 * 1: will wait till the consumer finished the previous record
 * 0: return falure if previous request is not finished yet
 */
static int waitReqFinish(struct request *req, int sync, int abort)
{
	if (!req)
		return -EFAULT;

	if (req->status == reqs_setup)
		return -EEXIST;

	while (req->status == reqs_wip ||
		req->status == reqs_sent ||
		req->status == reqs_setup)
	{
		if (!gen_loop)
			return -EFAULT;
		if (abort && hit_deadline(req))
			abort_request(req);
		else if (!sync)
			return -EBUSY;
	}

	/* logRequest will change the state to be reqs_initial */
	if (req->status == reqs_done)
		if (logRequest(req))
			return -EFAULT;

	return 0;
}

struct request *getRequest(int number)
{
	if (number > req_number)
		return NULL;

	return &request_array[number];	
}


static int dumpResult(struct request *req)
{
	tprintf("\nreq %d status %s\n", req->id, req_status_string[req->status]);
	if (!req)
		return -1;

	tprintf("sent request in total %lld missed %lld aborted %lld\n", req->stats.t_reqs,
		req->stats.t_missed, req->stats.t_aborted);

	return 0;
}

void dumpGenResults(void)
{
	int i;

	for(i=0; i < req_number; i++)
	{
		struct request *req = getRequest(i);
		if (req)
		{
			waitReqFinish(req, 1, 0);
			dumpResult(req);
		}
	}
}

void freeRequests(void)
{
	free(request_array);
}

int initRequests(int number)
{
	int i;

	if (request_array)
		return -EEXIST;

	if ((number <= 0) || number > MAX_APPS)
		return -EINVAL;

	request_array = calloc(number, sizeof(struct request));

	if (!request_array)
		return -ENOMEM;

	req_number = number;
	for (i=0; i < number; i++)
	{
		struct request *request = getRequest(i);

		request->status = reqs_initial;
		request->id = i;
	}

	return 0;
}

struct thread_param
{
	int id;
	/* XXX the only reason we need isolated CPU is because the window
	 * that we set the rtime and update status, otherwise we should be
	 * fine.
	 */
	int pCPU;
	struct request_config *config;
	struct request *req;
};

volatile int gen_loop;

/* sync:
 * 1: will wait till the consumer finished the previous record
 * 0: return falure if previous request is not finished yet
 *
 * target: the DPDK app
 * size: how many request
 * cost: how long for each request
 */
int sendRequest(int sync, int target, struct request_entry *rentry)
{
	struct request *req = getRequest(target);
	int oldstat;
	unsigned long long htime;

	if (!req)
		return -EFAULT;

	/* Why someone else is trying the setup now? */
	if (req->status == reqs_setup)
		return -EEXIST;

	while (gen_loop && waitReqFinish(req, 0, 1)) {}

	if (!gen_loop)
		return 0;

	oldstat = __sync_val_compare_and_swap(&req->status, reqs_initial,
			reqs_setup);
	if (oldstat != reqs_initial)
	{
		tprintf("initial status changed on the fly to %s, anything wrong?? \n", req_status_string[oldstat]);
		return -EFAULT;
	}

	req->stime = req->etime = 0;
	req->done = 0;
	req->eabort= 0;
        req->req.size = rentry->size;
        req->req.duration = rentry->duration;
	/* the 200 is the potential app's cost other than the execTask */
	htime = req->req.size * (rentry->duration+200);
	req->rtime = getNow();
	req->deadline = req->rtime + htime;
	oldstat = __sync_val_compare_and_swap(&req->status, reqs_setup, reqs_sent);
	if (oldstat != reqs_setup)
	{
		reqStatusWrong(oldstat, reqs_setup, reqs_sent);
		return -EFAULT;
	}

	return 0;
}

void *generator_thread(void *param)
{
	struct thread_param *par = param;
	int i=0, pcpu;
	cpu_set_t mask;
	struct request_config *config;
	struct request *req;

	if (!par)
		return NULL;

	pcpu = par->pCPU;
	if (pcpu)
	{
		pthread_t thread;

		CPU_ZERO(&mask);
		CPU_SET(pcpu, &mask);
		thread = pthread_self();
		if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &mask))
		{
			tprintf("set affinity failed \n"); 
			tprintf("affinity failed from pktgen.c \n");
			return NULL;
		}
	}

	config = par->config;
	req = par->req;
	while (gen_loop) {
		struct request_entry *entry;

		entry = &config->entries[i];
		sendRequest(0, par->id, entry);
		while (gen_loop && waitReqFinish(req, 0, 1)){};
		i++;
		if (i == config->entnum)
			i = 0;
	}

	return NULL;
}

/* XXX Hardcode now, can be configuration in future */
static int getPCpu(int id)
{
	return id + 1;
}

static struct thread_param **params = NULL;
static pthread_t *threads = NULL;
static int num_pktgens_initiated;

int init_pktgens(int num_pktgens, struct request_config *config)
{
	int i, result = 0;

	params = calloc(1, sizeof(struct thread_param *) * num_pktgens);
	if (!params)
		return -ENOMEM;

	threads = calloc(1, sizeof(pthread_t) * num_pktgens);
	if (!threads)
	{
		result = -ENOMEM;
		goto error;
	}

	for (i = 0; i < num_pktgens; i++)
	{
		pthread_attr_t attr;
		struct thread_param *par = NULL;
		int status;

		status = pthread_attr_init(&attr);
		if (status != 0){
			tprintf("error from pthread_attr_init for thread %d\n", i);
			result = -ENOMEM;
			break;
		}
		par = params[i] = calloc(1, sizeof(struct thread_param));
		if (!par )
		{
			tprintf("Failed to alloc thread param\n");
			result = -ENOMEM;
			break;
		}
		par->id = i;
		par->pCPU = getPCpu(i);	
		par->req = getRequest(i);
		par->config = &config[i];

		if (pthread_create(&threads[i], &attr, generator_thread, par))
		{
			tprintf("Failed to craete the thread\n");
			result = - ENOSYS;
			break;
		}
	}
error:
	if (result)
		free_pktgens();
	else
		num_pktgens_initiated = num_pktgens;
	return result;
}

void wait_pktgen_done(void)
{
	int i, num_apps = num_pktgens_initiated;

	if (!threads)
		return;

	for (i = 0; i < num_apps; i++)
	{
		if (threads[i])
		{
			int jret;
			tprintf("Join the packet gen thread %llx\n",
				(unsigned long long)threads[i]);
			jret = pthread_join(threads[i], NULL);
			if (jret)
				tprintf("jret failed %x\n", jret);
		}
	}

	free(threads);
	threads = NULL;
	//num_pktgens_initiated = 0;
}

void free_pktgens(void)
{
	int i;

	wait_pktgen_done();
	if (params)
	{
		for (i = 0; i < num_pktgens_initiated; i++)
			free(params[i]);
		//free(params);
	}
}
