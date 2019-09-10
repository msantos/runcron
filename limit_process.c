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
