#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "waitfor.h"

int waitfor(int *status) {
  for (;;) {
    errno = 0;
    if (wait(status) < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    return 0;
  }
}
