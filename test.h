#ifndef _TEST_HEAD_H
#define _TEST_HEAD_H

enum req_status {
	reqs_initial,
	reqs_setup,
	reqs_sent,
	reqs_wip,
	reqs_done,
};

extern char * req_status_string[];

struct req_statstic {
	unsigned long long t_reqs;
	unsigned long long t_missed;
};

/*
 * This represent one burst of packet sent out from the TG
 * Although we can simply specify the duration in total (i.e. one prameter
 * only, but it's more like a TG with size of packet number and duration as
 * packet size
 */ 
struct request_entry
{
	int size;
	int duration;
};

struct request_config
{
	int entnum;
	struct request_entry *entries;
};

struct test_config
{
	int num_apps;
	int num_pktgens;
	struct request_config *configs;
};

/*
 * This is a producer program to emulate the work of a packet generator.
 * It tries to feed packet requirement to the DPDKapp thread.
 */
struct request {
	int id;
	int status;
	/* request information */
	struct request_entry req;
	/* How many has been done */
	int done;
	/* the time the request is sent out */
	unsigned long long rtime;
	/* the time the request is expected to finish
	 * if the handling finished after this point, the
	 * DPDKapp should exit handling and then some packet lost
	 */
	unsigned long long deadline;
	/* The time the DPDKApp begin the handling */
	unsigned long long stime;
	/* The time the DPDKApp finish the handling */
	unsigned long long etime;

	struct req_statstic stats;
};

extern int req_number;
extern struct request *request_array;
extern struct request *getRequest(int number);
extern int initRequests(int number);
extern void freeRequests(void);
void dumpGenResults(void);
void dumpAppResults(void);
void wait_dpdk_done(void);
void wait_pktgen_done(void);

int init_dpdk_apps(int num_apps);
void free_dpdk_apps(void);
int init_pktgens(int num_pktgens, struct request_config *config);
void free_pktgens(void);
extern int gen_loop;
extern int app_loop;

typedef unsigned long long ttime_t;

static inline unsigned long long rdtscl(void)
{
	unsigned long low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return  ((low) | (high) << 32);
}

static inline ttime_t getNow(void)
{
	return (ttime_t)rdtscl();
}


#define MAX_APPS 0x4

#define KVM_HYPERCALL \
	(".byte 0x0f,0x01,0xc1", ".byte 0x0f,0x01,0xd9")

#endif
