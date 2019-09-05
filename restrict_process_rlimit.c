#include "runcron.h"
#ifdef RESTRICT_PROCESS_rlimit
#include <sys/resource.h>
#include <time.h>

int restrict_process() {
  struct rlimit rl_zero = {0};

  if (setrlimit(RLIMIT_NPROC, &rl_zero) < 0)
    return -1;

  if (setrlimit(RLIMIT_FSIZE, &rl_zero) < 0)
    return -1;

  return setrlimit(RLIMIT_NOFILE, &rl_zero);
}
#endif
