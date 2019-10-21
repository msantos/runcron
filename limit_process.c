/* Copyright (c) 2019, Michael Santos <michael.santos@gmail.com>
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
#include "runcron.h"
#include <sys/resource.h>
#include <time.h>

int limit_process(runcron_t *rp) {
  struct rlimit rl = {0};

#ifdef RLIMIT_CPU
  if (getrlimit(RLIMIT_CPU, &rl) < 0)
    return -1;

  rl.rlim_cur = rp->cpu;

  if (setrlimit(RLIMIT_CPU, &rl) < 0)
    return -1;
#endif

#ifdef RLIMIT_AS
  if (getrlimit(RLIMIT_AS, &rl) < 0)
    return -1;

  rl.rlim_cur = rp->as;

  if (setrlimit(RLIMIT_AS, &rl) < 0)
    return -1;
#endif

  return 0;
}
