

int main()
{
	int i,result;
	struct thread_param **params = NULL;
	pthread_t *threads = NULL;

	/* XXX Hardcode now till we make the loop_count more flexible */
	unsigned long lcounts[2]={0x200, 0x2000000};
	unsigned long tcounts[2]={0x8000000, 0x800};

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
		par->yield.logs = calloc(par->yield.logs_len,
					 sizeof(unsigned long));
		par->loop_count = lcounts[i];
		par->test_count = tcounts[i];
		if (!par->yield.logs)
		{
			printf("Failed to alloc logs buffer\n");
			result = -ENOMEM;
			break;
		}

		par->yielded.logs = calloc(par->yielded.logs_len,
					   sizeof(unsigned long));
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
