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
#include <sys/resource.h>
#include <sys/time.h>

typedef struct {
  int opt;
  int verbose;
  rlim_t cpu;
  rlim_t as;
} runcron_t;

enum {
  OPT_TIMESTAMP = 1 << 0,
  OPT_PRINT = 1 << 1,
  OPT_DRYRUN = 1 << 2,
  OPT_DISABLE_PROCESS_RESTRICTIONS = 1 << 3,
  OPT_LIMIT_CPU = 1 << 4,
  OPT_LIMIT_AS = 1 << 5,
  OPT_DISABLE_SIGNAL_ON_EXIT = 1 << 6,
  OPT_ALLOW_SETUID_SUBPROCESS = 1 << 7,
};
