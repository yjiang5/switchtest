#include <stdlib.h>
#include <stdio.h>
#include <sched.h>


#define KVM_HYPERCALL \
	(".byte 0x0f,0x01,0xc1", ".byte 0x0f,0x01,0xd9")

static int in_kvm;
unsigned long long max=0, min=(unsigned long long)-1, total, yield_max;

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

void dump_result(void)
{
	int i;
	printf("yield delay status \n");
	printf("max: %lld min %lld average %lld crazy yield\n", max, min, total/i, yield_max);
	printf("Total yielded \n", total_yielded);
	printf("maxium yielded is %lld crazy yielded is %lld\n", max_crazy_noise, prempt_crazy_noise);
	for (i=0; i< (MAX_NOISE_LOG >> 12); i++)
		if (prempt_log[i])
			printf("yielded %llx count %lx\n", i << 12, prempt_log[i]);
}

#define LOOP_COUNT 0x200000
int test(int update)
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
			return -1;
		}
		prempt = etsc - ptsc;
		/* Assume we are preempted when noise is > MAX_NOISE */
		if (prempt > MAX_NOISE)
		{
			/* Align to about 1us, since we don't care for the varia less than 1us */
			total_yielded ++;
			if (prempt > MAX_NOISE_LOG)
			{
				prempt_crazy_noise ++;
				if (prempt > max_crazy_noise)
					max_crazy_noise = prempt;
			}
			else
				prempt_log[prempt >> 12] ++;
		}
		ptsc = etsc;
	} while ((etsc - stsc ) < LOOP_COUNT);

	stsc = rdtscl();
	yield_exec();
	etsc = rdtscl();
	if (etsc < stsc){
		printf("Hit tscl wrap after yield\n");
	}

	delta = etsc - stsc;
	if (!update)
		return -1;
	if (delta > max)
		max = delta;
	if (delta < min)
		min = delta;
	total += delta;
	return 0;
}

#define TESTS	0xc0000

int main()
{
	int i;
	check_inkvm();

	test(0);
	for (i=0; i < TESTS; i++)
	{
		if (test(1))
		{
			printf("Something wrong on the test, exit now!\n");
			return -1;
		}
		if (i && max > 40000)
			yield_max++;
	}
	dump_result();
}
