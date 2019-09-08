#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#define _DEFAULT_SOURCE
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "waitfor.h"

typedef struct {
  int opt;
  int verbose;
} runcron_t;

enum {
  OPT_TIMESTAMP = 1,
  OPT_PRINT = 2,
  OPT_DRYRUN = 4,
  OPT_DISABLE_PROCESS_RESTRICTIONS = 4,
};

#ifndef HAVE_STRTONUM
long long strtonum(const char *numstr, long long minval, long long maxval,
                   const char **errstrp);
#endif

int cronevent(runcron_t *rp, char *cronentry, unsigned int *seconds,
              time_t now);
