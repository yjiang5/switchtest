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
	printf("old staus is %s while expected change from %s to %s\n",
		req_status_string[rold],
		req_status_string[eold],
		req_status_string[new]);
}

int logRequest(struct request *req)
{
	int oldstat;

	if (!req || !req->status != reqs_done)
		return -EFAULT;

	req->stats.t_reqs += req->req.size;
	req->stats.t_missed += (req->req.size - req->done);
	oldstat = __sync_val_compare_and_swap(&req->status, reqs_done, reqs_initial);
	if (oldstat != reqs_done)
	{
		reqStatusWrong(oldstat, reqs_done, reqs_initial);
		return -EFAULT;
	}

	return 0;
}

/*
 * Wait till the queue is clear
 * sync:
 * 1: will wait till the consumer finished the previous record
 * 0: return falure if previous request is not finished yet
 */
int waitReqFinish(struct request *req, int sync)
{
	if (!req)
		return -EFAULT;

	if (req->status == reqs_sent || req->status == reqs_setup)
		return -EEXIST;

	while (req->status == reqs_wip ||
		req->status == reqs_sent ||
		req->status == reqs_setup)
	{
		if (!sync)
			return -EBUSY;
		else
			delay();
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

	while (!waitReqFinish(req, sync))
		delay();

	oldstat = __sync_val_compare_and_swap(&req->status, reqs_initial,
			reqs_setup);
	if (oldstat != reqs_initial)
	{
		printf("initial status changed on the fly, anything wrong?? \n");
		return -EFAULT;
	}

	req->stime = req->etime = 0;
	req->done = 0;
        req->req.size = rentry->size;
        req->req.duration = rentry->duration;
	/* XXX will htime before getNow() really make rtime/deadline
	 * more accurate?
	 */
	htime = req->req.size * rentry->duration;
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

int dumpResult(struct request *req)
{
	if (!req || req->status != reqs_initial)
		return -1;

	printf("\nreq %d\n", req->id);
	printf("sent request in total %lld missed %lld\n", req->stats.t_reqs,
		req->stats.t_missed);

	return 0;
}

void dumpResults(void)
{
	int i;

	for(i=0; i < req_number; i++)
	{
		struct request *req = getRequest(i);
		if (req)
		{
			waitReqFinish(req, 1);
			dumpResult(req);
		}
	}
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

int gen_loop;

void *generator_thread(void *param)
{
	struct thread_param *par = param;
	int i=0;
	struct request_config *config;
	struct request *req;

	if (!par)
		return NULL;

	config = par->config;
	req = par->req;
	while (gen_loop) {
		struct request_entry *entry;

		if (i++ == config->entnum)
			i = 0;

		entry = &config->entries[i];
		sendRequest(0, par->id, entry);
		waitReqFinish(req, 1);
	}

	return NULL;
}

/* XXX Hardcode now, can be configuration in future */
static int getPCpu(int id)
{
	return id + 32;
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
			printf("error from pthread_attr_init for thread %d\n", i);
			result = -ENOMEM;
			break;
		}
		par = params[i] = calloc(1, sizeof(struct thread_param));
		if (!par )
		{
			printf("Failed to alloc thread param\n");
			result = -ENOMEM;
			break;
		}
		par->id = i;
		par->pCPU = getPCpu(i);	
		par->req = getRequest(i);
		par->config = config;

		if (pthread_create(&threads[i], &attr, generator_thread, par))
		{
			printf("Failed to craete the thread\n");
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

void free_pktgens(void)
{
	int i, num_pktgens = num_pktgens_initiated;

	for (i = 0; i < num_pktgens; i++)
		if (params[i])
			free(params[i]);

	if (params)
		free(params);

	for (i = 0; i < num_pktgens; i++)
	{
		if (threads[i])
		{
			int jret;

			printf("Join the pktgen thread %llx\n", (unsigned long long)threads[i]);
			jret = pthread_join(threads[i], NULL);
			if (jret)
				printf("jret failed %x\n", jret);
		}
	}
	if (threads)
		free(threads);
}
