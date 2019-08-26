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

#ifndef HAVE_STRTONUM
long long strtonum(const char *numstr, long long minval, long long maxval,
                   const char **errstrp);
#endif

unsigned int cronevent(char *cronentry, time_t now, int verbose);
