#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "log.h"
#include "fifo.h"
#include "socklib.h"
#include "udpcast.h"
#include "rateGovernor.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#if defined HAVE_DLSYM && defined NO_BB
#define DL_RATE_GOVERNOR
#endif

void *rgInitGovernor(struct net_config *cfg, struct rateGovernor_t *gov)
{
  if(cfg->nrGovernors == MAX_GOVERNORS) {
    fprintf(stderr, "Too many rate governors\n");
    exit(1);
  }
  cfg->rateGovernor[cfg->nrGovernors] = gov;
  return cfg->rateGovernorData[cfg->nrGovernors++] =  gov->rgInitialize();
}

#ifdef DL_RATE_GOVERNOR
void rgParseRateGovernor(struct net_config *net_config, char *rg)
{
    char *pos = strchr(rg, ':');
    char *dlname;
    char *params;
    char *error;
    void *rgdl;
    struct rateGovernor_t *gov;
    void *data;

    if(pos) {
	dlname = strndup(rg, pos-rg);
	params = pos+1;
    } else {
	dlname = rg;
	params = NULL;
    }

    rgdl = dlopen(dlname, RTLD_LAZY);
    if(rgdl == NULL) {
	fprintf(stderr, "Library load error %s\n", dlerror());
	exit(1);
    }
    dlerror(); /* Clear any existing error */

    gov = dlsym(rgdl, "governor");
    if ((error = dlerror()) != NULL)  {
	fprintf(stderr, "Symbol resolve error: %s\n", error);
	exit(1);
    }

    if(pos)
	free(dlname);

    data = rgInitGovernor(net_config, gov);

    if(net_config->rateGovernorData == NULL) {
	fprintf(stderr, "Rate governor initialization error\n");
	exit(1);
    }

    if(gov->rgSetProp) {
      while(params && *params) {
	char *eqPos; /* Position of the equal sign */
	const char *key; /* Property name */
	const char *value; /* property value */
	pos = strchr(params, ',');
	if(pos == NULL)
	  pos = params + strlen(params);
	eqPos = strchr(params, '=');
	if(eqPos == NULL || eqPos >= pos) {
	  key = strndup(params, pos-params);
	  value = NULL;
	} else {
	  key = strndup(params, eqPos-params);
	  value = strndup(eqPos+1, pos-(eqPos+1));
	}
	gov->rgSetProp(data, key, value);
	if(*pos)
	  pos++;
	params=pos;
      }
    }
    if(gov->rgEndConfig) {
      gov->rgEndConfig(data);
    }
}
#endif

void rgWaitAll(struct net_config *cfg, int sock, in_addr_t ip, int size)
{
  int i=0;
  for(i=0; i<cfg->nrGovernors; i++) {
    cfg->rateGovernor[i]->rgWait(cfg->rateGovernorData[i], sock, ip, size);
  }
}

void rgShutdownAll(struct net_config *cfg)
{
  int i=0;
  for(i=0; i<cfg->nrGovernors; i++) {
    if(cfg->rateGovernor[i]->rgShutdown)
	cfg->rateGovernor[i]->rgShutdown(cfg->rateGovernorData[i]);
  }
}

