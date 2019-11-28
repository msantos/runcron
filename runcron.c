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

#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>

#define RUNCRON_VERSION "0.7.0"

static int read_exit_status(int fd, int *status);
static int write_exit_status(int fd, int status);
void sleepfor(unsigned int seconds);
void wakeup(int sig);
void signal_handler(int sig);
int signal_wakeup(void);
int signal_init(void);
static void usage();

static const struct option long_options[] = {
    {"file", required_argument, NULL, 'f'},
    {"chdir", required_argument, NULL, 'C'},
    {"timeout", required_argument, NULL, 'T'},
    {"poll-interval", required_argument, NULL, 'P'},
    {"dryrun", no_argument, NULL, 'n'},
    {"print", no_argument, NULL, 'p'},
    {"signal", no_argument, NULL, 's'},
    {"limit-cpu", required_argument, NULL, OPT_LIMIT_CPU},
    {"limit-as", required_argument, NULL, OPT_LIMIT_AS},
    {"timestamp", required_argument, NULL, OPT_TIMESTAMP},
    {"disable-process-restrictions", no_argument, NULL,
     OPT_DISABLE_PROCESS_RESTRICTIONS},
    {"verbose", no_argument, NULL, 'v'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

pid_t pid;
int default_signal = SIGTERM;
int runnow = 0;
int remaining = 0;

int main(int argc, char *argv[]) {
  runcron_t *rp;
  char *file = ".runcron.lock";
  char *cwd = NULL;
  char *cronentry;
  int fd;
  int status = 0;
  time_t now;
  unsigned int seconds;
  unsigned int timeout = 0;
  int32_t poll_interval = 3600; /* 1 hour */
  const char *errstr = NULL;
  int exit_value = 0;

  int ch;

  if (setvbuf(stdout, NULL, _IOLBF, 0) < 0)
    err(EXIT_FAILURE, "setvbuf");

  rp = calloc(1, sizeof(runcron_t));

  if (rp == NULL)
    err(EXIT_FAILURE, "calloc");

  rp->cpu = 10;
  rp->as = 1 * 1024 * 1024;

  now = time(NULL);
  if (now == -1)
    err(EXIT_FAILURE, "time");

  (void)localtime(&now);

  while ((ch = getopt_long(argc, argv, "+C:f:hnpP:s:T:v", long_options,
                           NULL)) != -1) {
    switch (ch) {
    case 'C':
      cwd = optarg;
      break;

    case 'f':
      file = optarg;
      break;

    case 'n':
      rp->opt |= OPT_DRYRUN;
      break;

    case 'p':
      rp->opt |= OPT_PRINT;
      break;

    case 'P':
      poll_interval = strtonum(optarg, 0, INT_MAX, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case 's':
      default_signal = strtonum(optarg, 0, NSIG, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case 'T':
      timeout = strtonum(optarg, -1, UINT32_MAX, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case 'v':
      rp->verbose++;
      break;

    case OPT_LIMIT_CPU:
      rp->cpu = strtonum(optarg, -1, UINT32_MAX, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case OPT_LIMIT_AS:
      rp->as = strtonum(optarg, -1, UINT32_MAX, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case OPT_TIMESTAMP:
      now = timestamp(optarg);
      if (now == -1)
        errx(EXIT_FAILURE, "error: invalid timestamp: %s", optarg);
      break;

    case OPT_DISABLE_PROCESS_RESTRICTIONS:
      rp->opt |= OPT_DISABLE_PROCESS_RESTRICTIONS;
      break;

    case 'h':
    default:
      usage();
    }
  }

  argc -= optind;
  argv += optind;

  if (argc < 2)
    usage();

  cronentry = argv[0];

  argc--;
  argv++;

  if (cronevent(rp, cronentry, &seconds, now) < 0)
    exit(1);

  fd = open(file, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);

  if (fd < 0) {
    switch (errno) {
    case EEXIST:
      fd = open(file, O_RDWR | O_CLOEXEC, 0);
      if (fd < 0)
        err(111, "open: %s", file);
      if (read_exit_status(fd, &status) < 0)
        err(111, "read_exit_status: %s", file);
      break;
    default:
      err(111, "open: %s", file);
    }
  } else {
    /* @reboot */
    if (seconds == UINT32_MAX) {
      status = 255;
      seconds = 0;
    }
    if (write_exit_status(fd, status) < 0)
      err(111, "write_exit_status: %s", file);
  }

  if (!(rp->opt & OPT_DRYRUN) && (flock(fd, LOCK_EX | LOCK_NB) < 0))
    err(111, "flock");

  if ((cwd != NULL) && (chdir(cwd) < 0)) {
    err(111, "chdir: %s", cwd);
  }

  if (status != 0) {
    if (seconds > poll_interval) {
      seconds = poll_interval;
    }
  }

  if (rp->opt & OPT_PRINT)
    (void)printf("%lu\n", (long unsigned int)seconds);

  if (timeout == 0) {
    if (cronevent(rp, cronentry, &timeout, now + seconds) < 0)
      exit(1);
  }

  if (rp->verbose >= 1)
    (void)fprintf(
        stderr,
        "last exit status was %d, sleep interval is %ds, command timeout "
        "is %us\n",
        status, seconds, timeout);

  if (rp->opt & OPT_DRYRUN)
    exit(0);

  if (signal_wakeup() < 0)
    err(111, "signal_wakeup");

  sleepfor(seconds);

  pid = fork();

  switch (pid) {
  case -1:
    err(111, "fork");
  case 0:
    if (setpgid(0, 0) < 0)
      err(111, "setpgid");

    (void)execvp(argv[0], argv);
    exit(127);
  default:
    if (signal_init() < 0) {
      (void)kill(-pid, default_signal);
    }
    if (rp->verbose >= 1)
      (void)fprintf(stderr, "running command: timeout is set to %us\n",
                    timeout);
    if (timeout < UINT32_MAX) {
      alarm(timeout);
    }
    if (waitfor(&status) < 0) {
      (void)kill(-pid, default_signal);
      err(111, "waitfor");
    }
  }

  if (WIFEXITED(status))
    exit_value = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    exit_value = 128 + WTERMSIG(status);

  if (rp->verbose >= 3)
    (void)fprintf(stderr, "status=%d exit_value=%d\n", status, exit_value);

  if (write_exit_status(fd, exit_value) < 0)
    err(111, "write_exit_status: %s", file);

  exit(exit_value);
}

void sleepfor(unsigned int seconds) {
  while (seconds > 0 && !runnow) {
    if (remaining) {
      (void)fprintf(stderr, "%u\n", seconds);
      remaining = 0;
    }
    seconds = sleep(seconds);
  }
}

void wakeup(int sig) {
  switch (sig) {
  case SIGUSR1:
    runnow = 1;
    break;
  case SIGUSR2:
    remaining = 1;
    break;
  default:
    _exit(sig + 128);
  }
}

void signal_handler(int sig) {
  if (pid > 0)
    (void)kill(-pid, sig == SIGALRM ? default_signal : sig);
}

int signal_wakeup(void) {
  struct sigaction act = {0};

  act.sa_handler = wakeup;
  (void)sigfillset(&act.sa_mask);

  if (sigaction(SIGUSR1, &act, NULL) < 0)
    return -1;

  if (sigaction(SIGUSR2, &act, NULL) < 0)
    return -1;

  return 0;
}

int signal_init(void) {
  struct sigaction act = {0};
  int sig = 0;

  act.sa_handler = signal_handler;
  (void)sigfillset(&act.sa_mask);

  for (sig = 1; sig < NSIG; sig++) {
    if (sig == SIGCHLD)
      continue;

    if (sigaction(sig, &act, NULL) < 0) {
      if (errno == EINVAL)
        continue;

      return -1;
    }
  }

  return 0;
}

static int write_exit_status(int fd, int status) {
  unsigned char buf;

  buf = status > 255 ? 255 : (unsigned char)status;

  if (lseek(fd, 0, SEEK_SET) < 0)
    return -1;

  if (write(fd, &buf, 1) != 1)
    return -1;

  return 0;
}

static int read_exit_status(int fd, int *status) {
  unsigned char buf;

  if (lseek(fd, 0, SEEK_SET) < 0)
    return -1;

  if (read(fd, &buf, 1) != 1)
    return -1;

  *status = buf;
  return 0;
}

static void usage() {
  errx(EXIT_FAILURE,
       "[OPTION] <CRONTAB EXPRESSION> <command> <arg> <...>\n"
       "version: %s (using %s mode process restriction)\n\n"
       "-f, --file             lock file path (default: .runcron.lock)\n"
       "-T, --timeout          specify command timeout (seconds)\n"
       "-P, --poll-interval    interval to retry failed commands (default: "
       "3600s)\n"
       "-C, --chdir            change working directory\n"
       "-n, --dryrun           do nothing\n"
       "-p, --print            output seconds to next timespec\n"
       "-s, --signal           signal sent task on timeout (default: 15)\n"
       "-v, --verbose          verbose mode\n"
       "    --limit-cpu        restrict cpu usage of cron expression parsing\n"
       "    --limit-as         restrict memory (address space) of cron expression parsing\n"
       "    --timestamp <YY-MM-DD hh-mm-ss|@epoch>\n"
       "    --disable-process-restrictions\n"
       "                       do not fork cron expression processing\n",
       RUNCRON_VERSION, RESTRICT_PROCESS);
}
