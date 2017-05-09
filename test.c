#include <stdlib.h>
#include <stdio.h>
#include <sched.h>


#define KVM_HYPERCALL \
	(".byte 0x0f,0x01,0xc1", ".byte 0x0f,0x01,0xd9")

static int in_kvm;
unsigned long long max=0, min=(unsigned long long)-1, total;

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


#define LOOP_COUNT 0x1000
int test()
{
	unsigned long stsc, etsc, delta;

	stsc = rdtscl();
	do
	{
		etsc=rdtscl();
		if (etsc < stsc)
		{
			printf("Hit tscl wrap, exit\n");
			return -1;
			break;
		}

	} while ((etsc - stsc ) < LOOP_COUNT);

	stsc = rdtscl();
	yield_exec();
	etsc = rdtscl();
	if (etsc < stsc){
		printf("Hit tscl wrap after yield\n");
	}

	delta = etsc - stsc;
	if (delta > max)
		max = delta;
	if (delta < min)
		min = delta;
	total += delta;
	return 0;
}

#define TESTS	0x1000

int main()
{
	int i;
	check_inkvm();

	for (i=0; i < TESTS; i++)
	{
		if (test())
		{
			printf("Something wrong on the test, exit now!\n");
			return -1;
		}
	}
	printf("max: %llx min %llx average %llx\n", max, min, total/TESTS);
}
