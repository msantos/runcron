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

int limit_process() {
  struct rlimit rl = {0};

#ifdef RLIMIT_CPU
  /* XXX arbitrarily limit max CPU runtime to 10 (soft)/20 (hard) seconds */
  rl.rlim_cur = 10;
  rl.rlim_max = 20;

  if (setrlimit(RLIMIT_CPU, &rl) < 0)
    return -1;
#endif

#ifdef RLIMIT_AS
  /* XXX arbitrarily limit memoy usage to 1 MB (soft)/2 Mb (hard) */
  rl.rlim_cur = 1 * 1024 * 1024;
  rl.rlim_max = 2 * 1024 * 1024;

  if (setrlimit(RLIMIT_AS, &rl) < 0)
    return -1;
#endif

  return 0;
}
