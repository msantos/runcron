/* Copyright (c) 2019-2020, Michael Santos <michael.santos@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "timestamp.h"
#include "waitfor.h"

typedef struct {
  int opt;
  int verbose;
  rlim_t cpu;
  rlim_t as;
} runcron_t;

enum {
  OPT_TIMESTAMP = 1,
  OPT_PRINT = 2,
  OPT_DRYRUN = 4,
  OPT_DISABLE_PROCESS_RESTRICTIONS = 8,
  OPT_LIMIT_CPU = 16,
  OPT_LIMIT_AS = 32,
  OPT_DISABLE_SIGNAL_ON_EXIT = 64,
  OPT_ALLOW_SETUID_SUBPROCESS = 128,
};

#ifndef HAVE_STRTONUM
long long strtonum(const char *numstr, long long minval, long long maxval,
                   const char **errstrp);
#endif

int cronevent(runcron_t *rp, char *cronentry, unsigned int *seconds,
              time_t now);
