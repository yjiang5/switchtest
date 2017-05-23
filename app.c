#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */


#define KVM_HYPERCALL \
	(".byte 0x0f,0x01,0xc1", ".byte 0x0f,0x01,0xd9")

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

static inline unsigned long long rdtscl(void)
{
	unsigned long low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return  ((low) | (high) << 32);
}

int yield_log = 0x1000;
int yielded_log_of = 0x10000;
int yield_log_of = 0x10000;
int yielded_shift = 12;
int yield_shift = 12;
int pcpu = 33;

void dump_result(struct thread_param *result)
{
	int i;

	printf("yield delay status \n");
	printf("maxium yield is %lld max overflow %lld\n",
		result->yield.max, result->yield.of_max);
	for (i=0; i< (yield_log_of >> 12); i++)
		if (result->yield.logs[i])
			printf("yield %x count %ld\n", i << 12, result->yield.logs[i]);

	printf("Total yielded %lx \n", total_yielded);
	printf("maxium yielded is %lld max overflow %lld\n",
		result->yielded.max, result->yielded.of_max);
	for (i=0; i< (yielded_log_of >> 12); i++)
		if (result->yielded.logs[i])
			printf("yielded %x count %ld\n", i << 12, result->yielded.logs[i]);
}

void dump_results(struct thread_param **params)
{
	int i;

	for (i = 0; i < num_threads; i++)
	{
		printf("\ndump result %x\n", i);
		dump_result(params[i]);
	}
}

ttime_t PREEMPTION_THRESH = 0x1000;
ttime_t PREEMPTION_LOG_MAX = 0x2000;
ttime_t PREEMPTION_LOG_SHIFT = 12;
void log_preempted(struct sched_stats *preempt, ttime_t delta)
{
	if (delta > preempt->max)
		preempt->max = delta;

	if (delta > PREEMPTION_THRESH)
	{
		/* Align to about 1us, since we don't care for the varia less than 1us */
		preempt->logged ++;
		if (delta > MAX_PREEMPTION_LOG)
		{
			preempt->oflowed++;
			if (delta> preempt->of_max)
				preempt->of_max = delta;
		}
		else
			preempt->logs[delta >> PREEMPTION_LOG_SHIFT] ++;
	}
}

ttime_t YIELD_THRESH = 0x1000;
ttime_t YIELD_LOG_MAX = 0x2000;
ttime_t YIELD_LOG_SHIFT = 12;
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

ttime_t getNow(void)
{
	return (ttime_t)rdtscl();
}

int test(struct requet *req, struct sched_stats *prempt,
	 struct sched_stats *yield)
{
	ttime_t stsc, etsc, delta, ptsc;
	int duration, loops;

	while(waitReqReady());

	oldstat = __sync_val_compare_and_swap(&req->status, reqs_sent,
			reqs_wip);

	if (oldstat != 	reqs_sent)
	{
		printf("initial status changed on the fly, anything wrong?? \n");
		return -EFAULT;
	}
	
	req->stime = getNow();
	deadline = getDeadline(req->stime, req);
	while ((getNow() < deadline) && (req->done < req->req.size))
	{
		unsigned long prempt;

		stsc=getNow();
		execTask(req->req.duration);
		etsc = getNow();

		if (etsc < stsc)
		{
			printf("Hit time wrap, exit\n");
			return -EFAULT;
		}
		else {
			prempt = etsc - ptsc;
			if (prempt - req->req.duration > PREEMPTION_THRESH)
				log_preempted(result, prempt);
		}
		req->done++;
	}

	req->etime = getNow();
	oldstat = __sync_val_compare_and_swap(&req->status, reqs_wip,
			reqs_done);
	if (oldstat != 	reqs_wip)
	{
		printf("status changed on the guest fly, anything wrong?? \n");
		return -EFAULT;
	}

	stsc = getNow();
	yield_exec();
	etsc = getNow();
	if (etsc < stsc)
	{
		printf("Hit tscl wrap after yield\n");
	}
	else {
		delta = etsc - stsc;
		log_yield(result, delta);
	}

	return 0;
}

void *app_thread(void *param)
{
	int i;
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
			printf("set affinity failed\n");
			return NULL;
		}
	}

	while (app_loop) {
		if (test(par->req, par->preempted, par->yield))
		{
			printf("Something wrong on the test, exit now!\n");
			return NULL;
		}
	}
	return NULL;
}

/* XXX Hardcode now, can be configuration in future */
static int getPCpu(int id)
{
	return id + 22;
}

static struct thread_param **params = NULL;
static pthread_t *threads = NULL;
int init_apps(int num_apps)
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
		par->pcpu = getPCpu(i);	
		par->req = getRequest(i);

		par->yield.logs_len =  YIELD_LOG_MAX >> YIELD_LOG_SHIFT;
		par->yield.logs = calloc(par->yield.logs_len,
					 sizeof(ttime_t));
		par->preempted.logs_len =  PREEMPTION_LOG_MAX >> PREEMPTION_LOG_SHIFT;
		par->preempted.logs = calloc(par->preempted.logs_len,
					   sizeof(ttime_t));
		if (!par->yielded.logs || !par->preempted.logs)
		{
			printf("Failed to alloc logs buffer\n");
			result = -ENOMEM;
			break;
		}
		if (pthread_create(&threads[i], &attr, app_thread, par))
		{
			printf("Failed to craete the thread\n");
			result = - ENOSYS;
			break;
		}
	}
error:
	if (result)
		free_apps();
	return result;

}

void free_apps(void)
{
	int i;

	for (i = 0; i < num_apps; i++)
		if (params[i]){
			if (params[i]->preempted.logs)
				free(params[i]->yielded.logs);
			if (params[i]->yield.logs)
				free(params[i]->yield.logs);
			free(params[i]);
		}
	if (params)
		free(params);

	for (i = 0; i < num_apps; i++)
	{
		if (threads[i])
		{
			int jret;
			printf("Join the thread %llx\n", (unsigned long long)threads[i]);
			jret = pthread_join(threads[i], NULL);
			if (jret)
				printf("jret failed %x\n", jret);
		}
	}

	if (threads)
		free(threads);
}
