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

static int in_kvm;

struct sched_stats
{
	unsigned long logged;
	unsigned long oflowed;
	unsigned long long of_max;
	unsigned long long max;
	unsigned long *logs;
	unsigned long logs_len;
};

struct thread_param
{
	int id;
	int pcpu;
	struct request *req;
	struct sched_stats *yield;
	struct sched_stats *preempted;
};

static struct thread_param **params = NULL;
static pthread_t *threads = NULL;

unsigned long long prempt_crazy_noise, max_crazy_noise;

/* We only log the scheduler situation from MAX_NOISE to MAX_NOISE_LOGS */
#define MAX_NOISE	0x30000
#define MAX_NOISE_LOG	0x1000000
unsigned long prempt_log[MAX_NOISE_LOG >> 12], total_yielded;

int yield_exec(void)
{
	if (!in_kvm)
	{
		sched_yield();
	} else
	{
		int ret;
		int hc=1,p1=1,p2=1;
		asm volatile(".byte 0x0f,0x01,0xc1\n\t"
			     ".byte 0x0f,0x01,0xd9\n\t"
			     : "=a"(ret)
			     :
			     "a"(hc),
			     "b"(p1),
			     "c"(p2)
			     :
			     "memory");
	}
	return 0;
}

/* XXX hardcode here, will change to cpuid when begin the KVM task */
int check_inkvm(void)
{
	in_kvm = 0;
	return 0;
}

ttime_t YIELD_THRESH = 0x1000;
ttime_t YIELD_LOG_MAX = 0x2000;
ttime_t YIELD_LOG_SHIFT = 12;

ttime_t PREEMPTION_THRESH = 0x1000;
ttime_t PREEMPTION_LOG_MAX = 0x2000;
ttime_t PREEMPTION_LOG_SHIFT = 12;
static void dumpResult(struct thread_param *result)
{
	int i;

	tprintf("yield delay status \n");
	tprintf("maxium yield is %lld max overflow %lld\n",
		result->yield->max, result->yield->of_max);
	for (i=0; i< (YIELD_LOG_MAX>> 12); i++)
		if (result->yield->logs[i])
			tprintf("yield %x count %ld\n", i << 12, result->yield->logs[i]);

	tprintf("Total preempted %lx \n", result->preempted->logged);
	tprintf("maxium preempted is %lld max overflow %lld\n",
		result->preempted->max, result->preempted->of_max);
	for (i=0; i< (PREEMPTION_LOG_MAX >> 12); i++)
		if (result->preempted->logs[i])
			tprintf("preempted %x count %ld\n", i << 12, result->preempted->logs[i]);
}

static int num_apps_initiated = 0;
void dumpAppResults(void)
{
	int i;

	for (i = 0; i < num_apps_initiated; i++)
	{
		tprintf("\ndump result %x\n", i);
		dumpResult(params[i]);
	}
}

void log_preempted(struct sched_stats *preempt, ttime_t delta)
{
	if (delta > preempt->max)
		preempt->max = delta;

	if (delta > PREEMPTION_THRESH)
	{
		/* Align to about 1us, since we don't care for the varia less than 1us */
		preempt->logged ++;
		if (delta > PREEMPTION_LOG_MAX)
		{
			preempt->oflowed++;
			if (delta> preempt->of_max)
				preempt->of_max = delta;
		}
		else
			preempt->logs[delta >> PREEMPTION_LOG_SHIFT] ++;
	}
}

/* XXX possibly we can merge with the log_yielded if we are sure they are
 * totally similar
 */
void log_yield(struct sched_stats* yield, ttime_t delta)
{
	if (delta > yield->max)
		yield->max = delta;
	if (delta > YIELD_THRESH)
	{
		/* Align to about 1us, since we don't care for the varia less than 1us */
		yield->logged ++;
		if (delta> YIELD_LOG_MAX)
		{
			yield->oflowed++;
			if (delta> yield->of_max)
				yield->of_max = delta;
		}
		else
			yield->logs[delta >> YIELD_LOG_SHIFT] ++;
	}
}

int waitReqReady(struct request *req, int sync)
{
	if (!req)
		return -EFAULT;

	if (req->status != reqs_sent)
		return -EBUSY;

	return 0;
}

ttime_t getDeadline(ttime_t now, struct request *req)
{
	return now + (req->deadline - req->rtime);
}

static int execTask(ttime_t duration)
{
	while (duration)
		duration--;
	return 0;
}

int test(struct request *req, struct sched_stats *prempt,
	 struct sched_stats *yield)
{
	ttime_t stsc, etsc, delta, deadline;
	int oldstat;

	while(waitReqReady(req, 0) && app_loop);

	oldstat = __sync_val_compare_and_swap(&req->status, reqs_sent,
			reqs_wip);

	if (oldstat != 	reqs_sent)
	{
		tprintf("initial status changed on the fly, anything wrong?? \n");
		return -EFAULT;
	}
	
	req->stime = getNow();
	deadline = getDeadline(req->stime, req);
	while ((getNow() < deadline) && (req->done < req->req.size))
	{
		stsc=getNow();
		execTask(req->req.duration);
		etsc = getNow();

		if (etsc < stsc)
		{
			tprintf("Hit time wrap, exit\n");
			return -EFAULT;
		}
		else {
			delta = etsc - stsc;
			if ((delta - req->req.duration) > PREEMPTION_THRESH)
				log_preempted(prempt, delta);
		}
		req->done++;
	}

	req->etime = getNow();
	oldstat = __sync_val_compare_and_swap(&req->status, reqs_wip,
			reqs_done);
	if (oldstat != 	reqs_wip)
	{
		tprintf("status changed on the guest fly, anything wrong?? \n");
		return -EFAULT;
	}

	stsc = getNow();
	yield_exec();
	etsc = getNow();
	if (etsc < stsc)
	{
		tprintf("Hit tscl wrap after yield\n");
	}
	else {
		delta = etsc - stsc;
		log_yield(yield, delta);
	}

	return 0;
}

volatile int app_loop;

void *app_thread(void *param)
{
	struct thread_param *par = param;
	int pcpu;
	cpu_set_t mask;
	pthread_t thread;

	pcpu = par->pcpu;
	if (pcpu)
	{
		CPU_ZERO(&mask);
		CPU_SET(pcpu, &mask);
		thread = pthread_self();
		if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &mask))
		{
			tprintf("set affinity failed\n");
			return NULL;
		}
	}

	while (app_loop) {
		if (test(par->req, par->preempted, par->yield))
		{
			tprintf("Something wrong on the test, exit now!\n");
			return NULL;
		}
	}
	return NULL;
}

/* XXX Hardcode now, can be configuration in future */
static int getPCpu(int id)
{
	//return id + 22;
	return  22;
}

int init_dpdk_apps(int num_apps)
{
	int i,result = 0;

	params = calloc(1, sizeof(struct thread_param *) * num_apps);
	if (!params)
		return -ENOMEM;

	threads = calloc(1, sizeof(pthread_t) * num_apps);
	if (!threads)
	{
		result = -ENOMEM;
		goto error;
	}

	for (i = 0; i < num_apps; i++)
	{
		pthread_attr_t attr;
		int status;
		struct thread_param *par = NULL;

		status = pthread_attr_init(&attr);
		if (status != 0){
			tprintf("error from pthread_attr_init for thread %d\n", i);
			result = -ENOMEM;
			break;
		}

		par = params[i] = calloc(1,
			 sizeof(struct thread_param)+
				 2*sizeof(struct sched_stats));
		if (!par )
		{
			tprintf("Failed to alloc thread param\n");
			result = -ENOMEM;
			break;
		}

		par->yield = (struct sched_stats *)(
			(void *)par + sizeof(struct thread_param));
		par->preempted= (struct sched_stats *)(
			(void *)(par->yield) + sizeof(struct thread_param));
		par->id = i;
		par->pcpu = getPCpu(i);	
		par->req = getRequest(i);

		par->yield->logs_len =  YIELD_LOG_MAX >> YIELD_LOG_SHIFT;
		par->yield->logs = calloc(par->yield->logs_len,
					 sizeof(ttime_t));
		par->preempted->logs_len =  PREEMPTION_LOG_MAX >> PREEMPTION_LOG_SHIFT;
		par->preempted->logs = calloc(par->preempted->logs_len,
					   sizeof(ttime_t));
		if (!par->yield->logs || !par->preempted->logs)
		{
			tprintf("Failed to alloc logs buffer\n");
			result = -ENOMEM;
			break;
		}
		if (pthread_create(&threads[i], &attr, app_thread, par))
		{
			tprintf("Failed to craete the thread\n");
			result = - ENOSYS;
			break;
		}
	}
error:
	if (result)
		free_dpdk_apps();
	else
		num_apps_initiated = num_apps;
	return result;
}

void wait_dpdk_done(void)
{
	int i, num_apps = num_apps_initiated;

	if (!threads)
		return;

	for (i = 0; i < num_apps; i++)
	{
		if (threads[i])
		{
			int jret;
			tprintf("Join the app thread %llx\n",
				(unsigned long long)threads[i]);
			jret = pthread_join(threads[i], NULL);
			if (jret)
				tprintf("jret failed %x\n", jret);
		}
	}
	free(threads);
	threads = NULL;
	num_apps_initiated = 0;
}

void free_dpdk_apps(void)
{
	int i, num_apps = num_apps_initiated;
	/* In case thread is not terminated yet */
	wait_dpdk_done();
	for (i = 0; i < num_apps; i++)
		if (params[i]){
			if (params[i]->preempted->logs)
				free(params[i]->preempted->logs);
			if (params[i]->yield->logs)
				free(params[i]->yield->logs);
			free(params[i]);
		}
	if (params)
		free(params);
}
