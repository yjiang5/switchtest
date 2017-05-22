#ifndef _TEST_HEAD_H
#define _TEST_HEAD_H

enum req_status {
	reqs_initial,
	reqs_setup,
	reqs_sent,
	reqs_wip,
	reqs_done,
};

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

	struct req_stats stats;
};

extern int req_number;
extern struct request *request_array;
extern struct request *getRequest(int number);

#define MAX_APPS 0x4

#endif
