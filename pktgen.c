
int req_number;
struct request *request_array;

struct request *getRequest(int number)
{
	if (number > req_number)
		return NULL;

	return &request_array[i];	
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
	/* XXX will htime before getCurTime() really make rtime/deadline
	 * more accurate?
	 */
	htime = size * rentry->duration;
	req->rtime = getCurTime();
	req->deadline = req->rtime + htime;
	oldstat = __sync_val_compare_and_swap(&req->status, reqs_setup, reqs_sent);
	if (oldstat != reqs_setup)
	{
		printf("stat changed from reqs_setup to %s\n", req_status_string(oldstat));
		return -EFAULT;
	}

	return 0;
}

int logRequest(struct request *req)
{
	if (!req || !req->status != reqs_done)
		return -EFAULT;

	req->stats.t_reqs += req->rsize;
	req->stats.t_missed += (req->rsize - req->done);
	oldstat = __sync_val_compare_and_swap(&req->status, reqs_done, reqs_initial);
	if (oldstat != reqs_done)
	{
		reqStatusWrong(oldstat, reqs_done);
		return -EFAULT;
	}

	return 0;
}

int dumpResult(struct request *req)
{
	if (!req || req->status != reqs_initial)
		return -1;

	printf("\nreq %d\n", req->id);
	printf("sent request in total %d missed %d\n", req->stats.t_reqs,
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
			waitReqFinish(req);
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
		struct request = getRequest(i);

		request.status = reqs_initial;
		request.id = i;
	}

	return 0;
}


struct request_config
{
	int entnum;
	struct request_entry *entries;
};

struct thread_param
{
	int id;
	struct request_config *config;
	struct request *req;
};

void *generator_thread(void *param)
{
	struct thread_param *par = param;
	int i;
	struct request_config *config;
	struct request *req;

	if (!par)
		return NULL;

	config = par->config;
	req = getRequest(config->id);
	while (gen_loop) {
		struct request_entry *entry;

		if (i++ == config->entnum)
			i = 0;

		entry = config->entries[i];
		sendRequest(0, config->id, entry);
		waitReqFinish(req);
	}

	return NULL;
}

