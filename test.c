#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"

struct test_config
{
	int num_apps;
	int num_pktgens;
	struct request_config *configs;
};

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
		entry += sizeof(struct request_entry) * 1;
	}

	return confs;
}

int main()
{
	tconfs = init_configs();
	initRequests(2);
	init_dpdk_apps(tconfs->num_apps);
	init_pktgens(tconfs->num_pktgens, tconfs->configs);
	sleep (20);
	free_dpdk_apps();
	free_pktgens();
	return 0;
}
