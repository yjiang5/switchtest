#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"

static struct test_config *tconfs = NULL;
/* We hardcode everything now, we can make it configured through
 * file in future when we do more complex testing
 */

void free_configs(struct test_config *confs)
{
	if (!confs)
		return;

	free(confs->configs);
	free(confs);
}

struct test_config *init_configs()
{
	struct test_config *confs;
	int i;
	char *entry;

	confs = calloc(1, sizeof(struct test_config));
	if (!confs)
		return NULL;

	confs->configs = malloc(confs->num_pktgens *
					 sizeof(struct request_config) +
				/* hard coded number */
				confs->num_pktgens * 1 * sizeof(struct request_entry));
	if (!confs->configs)
	{
		free(confs);
		return NULL;
	}

	entry = (char *)confs->configs +
		 confs->num_pktgens * sizeof(struct request_config);
	confs->num_apps = 2;
	confs->num_pktgens = 2;
	for (i = 0; i < confs->num_pktgens; i++)
	{
		struct request_config *rconf = &confs->configs[i];
		rconf->entnum = 1;
		rconf->entries = (struct request_entry *)entry;
		entry += sizeof(struct request_entry) * rconf->entnum;
	}

	return confs;
}

int main()
{
	tconfs = init_configs();
	initRequests(2);
	app_loop = gen_loop = 1;
	init_dpdk_apps(tconfs->num_apps);
	init_pktgens(tconfs->num_pktgens, tconfs->configs);
	sleep(2);
	app_loop = gen_loop = 0;
	/* We stop the generator first */
	wait_pktgen_done();
	wait_dpdk_done();
	dumpAppResults();
	dumpGenResults();
	free_dpdk_apps();
	free_pktgens();
	freeRequests();
	return 0;
}
