#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "test.h"

static pthread_mutex_t printf_mutex;
int tprintf(const char *format, ...)
{
	va_list args;
	va_start(args, format);

	pthread_mutex_lock(&printf_mutex);
	vprintf(format, args);
	pthread_mutex_unlock(&printf_mutex);

	va_end(args);
	return 0;
}

static struct test_config *tconfs = NULL;
/* We hardcode everything now, we can make it configured through
 * file in future when we do more complex testing
 */

void free_configs(struct test_config *confs)
{
	int i;

	if (!confs)
		return;

	for (i=0; i<confs->num_pktgens; i++)
		if (confs->configs[i].entries)
			free(confs->configs[i].entries);
	free(confs->configs);
	free(confs);
}

struct test_config *init_configs()
{
	struct test_config *confs;
	int i;
	struct request_entry *entry;

	confs = calloc(1, sizeof(struct test_config));
	if (!confs)
		return NULL;

	confs->num_apps = APPS_NUM;
	confs->num_pktgens = PKTG_NUM;
	confs->configs = calloc(confs->num_pktgens,
					 sizeof(struct request_config));
	if (!confs->configs)
	{
		free(confs);
		return NULL;
	}

	for (i = 0; i < confs->num_pktgens; i++)
	{
		struct request_config *rconf = &confs->configs[i];

		rconf->entnum = PKTG_CONFIG_NUM;
		entry = calloc(rconf->entnum, sizeof(struct request_entry));
		if (!entry)
			goto error;
		rconf->entries = (struct request_entry *)entry;
		/* Hardcode the config now, assume it takes 200 cycle,
		 * which is similar to 64 byte packet, for the packet handling
		 * and we have 128 burst flow.
		 */
		rconf->entries[0].size = 128;
		rconf->entries[0].duration = 200;
	}

	return confs;
error:
	free_configs(confs);
	return NULL;
}

int main(int argc, char *argv[])
{
	int c;
	/* By default, 2 seconds */
	int duration = 2;

	opterr = 0;

	while ((c = getopt (argc, argv, "t:")) != -1)
	{
		switch (c)
		{
		case 't':
		{
			char *eptr;

			errno = 0;
			duration = strtol(optarg, &eptr, 0);
			printf("duration is %d\n", duration);
			if ((errno == ERANGE && (duration== LONG_MAX || duration== LONG_MIN))
					|| (eptr && *eptr))
			{
				printf("Error value\n");
				return -1;
			}
			break;
		}
		case '?':
			if (optopt == 't')
				printf("Option -t requires an duration argument.\n");
			else if (isprint (optopt))
				fprintf (stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf (stderr,
						"Unknown option character `\\x%x'.\n",
						optopt);
			return 1;
		default:
			abort();
		}
	}
	pthread_mutex_init(&printf_mutex, NULL);
	tconfs = init_configs();
	initRequests(PKTG_NUM);
	app_loop = gen_loop = 1;
	init_dpdk_apps(tconfs->num_apps);
	init_pktgens(tconfs->num_pktgens, tconfs->configs);
	sleep(duration);
	app_loop = gen_loop = 0;
	/* We stop the generator first */
	wait_pktgen_done();
	wait_dpdk_done();
	dumpAppResults();
	dumpGenResults();
	free_dpdk_apps();
	free_pktgens();
	freeRequests();
	free_configs(tconfs);
	return 0;
}
