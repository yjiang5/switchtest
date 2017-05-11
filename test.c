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
	struct sched_stats yield;
	struct sched_stats yielded;
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

int yielded_log = 0x1000;
int yield_log = 0x1000;
int yielded_log_of = 0x10000;
int yield_log_of = 0x10000;
int yielded_shift = 12;
int yield_shift = 12;
int num_threads = 2;
int pcpu = 33;

void dump_result(struct thread_param *result)
{
	int i;

	printf("yield delay status \n");
	printf("maxium yield is %lld max overflow %lld\n",
		result->yield.max, result->yield.of_max);
	for (i=0; i< (yield_log_of >> 12); i++)
		if (result->yield.logs[i])
			printf("yield %x count %lx\n", i << 12, result->yield.logs[i]);

	printf("Total yielded %lx \n", total_yielded);
	printf("maxium yielded is %lld max overflow %lld\n",
		result->yielded.max, result->yielded.of_max);
	for (i=0; i< (yielded_log_of >> 12); i++)
		if (result->yielded.logs[i])
			printf("yielded %x count %lx\n", i << 12, result->yield.logs[i]);
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

void log_yielded(struct thread_param *result, unsigned long long delta)
{
	if (delta > result->yielded.max)
		result->yielded.max = delta;
	if (delta > yielded_log)
	{
		/* Align to about 1us, since we don't care for the varia less than 1us */
		result->yielded.logged ++;
		if (delta> yielded_log_of)
		{
			result->yielded.oflowed++;
			if (delta> result->yielded.of_max)
				result->yielded.of_max = delta;
		}
		else
			result->yielded.logs[delta >> yielded_shift] ++;
	}
}

/* XXX possibly we can merge with the log_yielded if we are sure they are
 * totally similar
 */
void log_yield(struct thread_param *result, unsigned long long delta)
{
	if (delta > result->yield.max)
		result->yield.max = delta;
	if (delta > yield_log)
	{
		/* Align to about 1us, since we don't care for the varia less than 1us */
		result->yield.logged ++;
		if (delta> yield_log_of)
		{
			result->yield.oflowed++;
			if (delta> result->yield.of_max)
				result->yield.of_max = delta;
		}
		else
			result->yielded.logs[delta >> yield_shift] ++;
	}
}
#define LOOP_COUNT 0x20000
int test(struct thread_param *result)
{
	unsigned long stsc, etsc, delta, ptsc;

	ptsc=stsc = rdtscl();
	do
	{
		unsigned long prempt;

		etsc=rdtscl();
		if (etsc < ptsc)
		{
			printf("Hit tscl wrap, exit\n");
		}
		else {
			prempt = etsc - ptsc;
			log_yielded(result, prempt);
		}
		/* Assume we are preempted when noise is > MAX_NOISE */
		ptsc = etsc;
	} while ((etsc - stsc ) < LOOP_COUNT);

	stsc = rdtscl();
	yield_exec();
	etsc = rdtscl();
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

#define TESTS	0xc000

void *test_thread(void *param)
{
	int i;
	struct thread_param *par = param;
	cpu_set_t mask;
	pthread_t thread;

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

	for (i=0; i < TESTS; i++)
	{
		if (test(par))
		{
			printf("Something wrong on the test, exit now!\n");
			return NULL;
		}
	}
	return NULL;
}


int main()
{
	int i,result;
	struct thread_param **params = NULL;
	pthread_t *threads = NULL;

	check_inkvm();
	params = calloc(1, sizeof(struct thread_param *) * num_threads);
	if (!params)
		return -ENOMEM;

	threads = calloc(1, sizeof(pthread_t) * num_threads);
	if (!threads)
		goto error;

	result = 0;
	for (i = 0; i < num_threads; i++)
	{
		struct thread_param *par;
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
		par->yield.logs_len =  yield_log_of >> 12;
		par->yielded.logs_len =  yielded_log_of >> 12;
		par->yield.logs = calloc(par->yield.logs_len, sizeof(unsigned long));
		if (!par->yield.logs)
		{
			printf("Failed to alloc logs buffer\n");
			result = -ENOMEM;
			break;
		}

		par->yielded.logs = calloc(par->yielded.logs_len, sizeof(unsigned long));
		if (!par->yielded.logs)
		{
			printf("Failed to alloc logs buffer\n");
			result = -ENOMEM;
			break;
		}
		if (pthread_create(&threads[i], &attr, test_thread, par))
		{
			printf("Failed to craete the thread\n");
			result = - ENOSYS;
			break;
		}
	}

	if (result)
	{
		printf("something wrong\n");
		goto error;
	}

	usleep(20000);
	for (i = 0; i < num_threads; i++)
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
	dump_results(params);

error:
	for (i = 0; i < num_threads; i++)
		if (params[i]){
			if (params[i]->yielded.logs)
				free(params[i]->yielded.logs);
			if (params[i]->yield.logs)
				free(params[i]->yield.logs);
			free(params[i]);
		}
	if (params)
		free(params);

	if (threads)
		free(threads);
	return 0;
}
