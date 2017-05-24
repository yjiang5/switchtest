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

	confs->num_apps = 2;
	confs->num_pktgens = 2;
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

		rconf->entnum = 1;
		entry = calloc(rconf->entnum, sizeof(struct request_entry));
		if (!entry)
			goto error;
		rconf->entries = (struct request_entry *)entry;
	}

	return confs;
error:
	free_configs(confs);
	return NULL;
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
	free_configs(tconfs);
	return 0;
}
