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
#include "runcron.h"

#include "restrict_process.h"

#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "fnv1a.h"

#define RUNCRON_VERSION "0.9.0"

static int read_exit_status(int fd, int *status);
static int write_exit_status(int fd, int status);
void sleepfor(unsigned int seconds);
int signal_init(void (*handler)(int));
void sa_handler_sleep(int sig);
void sa_handler_wait(int sig);
static void print_argv(int argc, char *argv[]);
static void randinit(char *tag);
static void usage();

static const struct option long_options[] = {
    {"file", required_argument, NULL, 'f'},
    {"chdir", required_argument, NULL, 'C'},
    {"tag", required_argument, NULL, 't'},
    {"timeout", required_argument, NULL, 'T'},
    {"poll-interval", required_argument, NULL, 'P'},
    {"dryrun", no_argument, NULL, 'n'},
    {"print", no_argument, NULL, 'p'},
    {"signal", required_argument, NULL, 's'},
    {"limit-cpu", required_argument, NULL, OPT_LIMIT_CPU},
    {"limit-as", required_argument, NULL, OPT_LIMIT_AS},
    {"timestamp", required_argument, NULL, OPT_TIMESTAMP},
    {"allow-setuid-subprocess", no_argument, NULL, OPT_ALLOW_SETUID_SUBPROCESS},
    {"disable-process-restrictions", no_argument, NULL,
     OPT_DISABLE_PROCESS_RESTRICTIONS},
    {"disable-signal-on-exit", no_argument, NULL, OPT_DISABLE_SIGNAL_ON_EXIT},
    {"verbose", no_argument, NULL, 'v'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

pid_t pid;
int default_signal = SIGTERM;
volatile sig_atomic_t runnow = 0;
volatile sig_atomic_t remaining = 0;

int main(int argc, char *argv[]) {
  runcron_t *rp;
  char *file = ".runcron.lock";
  char *cwd = NULL;
  char *cronentry;
  char *tag = NULL;
  int fd;
  int status = 0;
  time_t now;
  unsigned int seconds;
  unsigned int timeout = 0;
  int32_t poll_interval = 3600; /* 1 hour */
  const char *errstr;
  int exit_value = 0;
  int signal_on_exit = 1;
  int allow_setuid_subprocess = 0;

  int ch;

  if (restrict_process_init() < 0)
    err(EXIT_FAILURE, "restrict_process_init");

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

  while ((ch = getopt_long(argc, argv, "+C:f:hnpP:s:t:T:v", long_options,
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

    case 't':
      tag = optarg;
      break;

    case 'T':
      timeout = strtonum(optarg, -1, UINT32_MAX, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case 'v':
      rp->verbose++;
      break;

    case OPT_ALLOW_SETUID_SUBPROCESS:
      allow_setuid_subprocess = 1;
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

    case OPT_DISABLE_SIGNAL_ON_EXIT:
      signal_on_exit = 0;
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

  randinit(tag);

  if (!allow_setuid_subprocess && disable_setuid_subprocess() < 0)
    err(111, "disable_setuid_subprocess");

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

  if (rp->verbose >= 1) {
    print_argv(argc, argv);
    (void)fprintf(
        stderr,
        ": last exit status was %d, sleep interval is %ds, command timeout "
        "is %us\n",
        status, seconds, timeout);
  }

  if (rp->opt & OPT_DRYRUN)
    exit(0);

  if (signal_init(sa_handler_sleep) < 0)
    err(111, "signal_init");

  sleepfor(seconds);

  if (status == 0) {
    if (write_exit_status(fd, 128 + SIGKILL) < 0)
      err(111, "write_exit_status: %s", file);
  }

  pid = fork();

  switch (pid) {
  case -1:
    err(111, "fork");
  case 0:
    if (setpgid(0, 0) < 0)
      err(111, "setpgid");

    if (restrict_process_signal_on_supervisor_exit() < 0)
      err(111, "restrict_process_signal_on_supervisor_exit");

    (void)execvp(argv[0], argv);
    exit(127);
  default:
    if (signal_init(sa_handler_wait) < 0) {
      (void)kill(-pid, default_signal);
    }
    if (rp->verbose >= 1) {
      print_argv(argc, argv);
      (void)fprintf(stderr, ": running command: timeout is set to %us\n",
                    timeout);
    }
    if (timeout < UINT32_MAX) {
      alarm(timeout);
    }
    if (waitfor(&status) < 0) {
      (void)kill(-pid, default_signal);
      err(111, "waitfor");
    }
    alarm(0);
  }

  if (WIFEXITED(status))
    exit_value = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    exit_value = 128 + WTERMSIG(status);

  if (rp->verbose >= 3)
    (void)fprintf(stderr, "status=%d exit_value=%d\n", status, exit_value);

  if (write_exit_status(fd, exit_value) < 0)
    err(111, "write_exit_status: %s", file);

  if (signal_on_exit) {
    (void)kill(-pid, default_signal);
  }

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

void sa_handler_sleep(int sig) {
  switch (sig) {
  case SIGUSR1:
    runnow = 1;
    break;
  case SIGUSR2:
    remaining = 1;
    break;
  case SIGINT:
    _exit(128 + SIGINT);
    break;
  default:
    break;
  }
}

void sa_handler_wait(int sig) {
  if (pid > 0)
    (void)kill(-pid, sig == SIGALRM ? default_signal : sig);
}

int signal_init(void (*handler)(int)) {
  struct sigaction act = {0};
  int sig;

  act.sa_handler = handler;
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

static void print_argv(int argc, char *argv[]) {
  int i;
  int space = 0;
  for (i = 0; i < argc; i++) {
    (void)fprintf(stderr, "%s%s", (space == 1 ? " " : ""), argv[i]);
    space = 1;
  }
}

static void randinit(char *tag) {
  uint32_t seed;
  char name[MAXHOSTNAMELEN] = {0};
  size_t len;

  if (tag == NULL) {
    if (gethostname(name, sizeof(name) - 1) < 0)
      err(EXIT_FAILURE, "gethostname");
    tag = name;
  }

  len = strlen(tag);

  if (len == 0) {
    struct timeval tv = {0};
    if (gettimeofday(&tv, NULL) < 0)
      err(EXIT_FAILURE, "gettimeofday");
    seed = getpid() ^ tv.tv_sec ^ tv.tv_usec;
  } else {
    seed = fnv1a((uint8_t *)tag, len);
  }

#if defined(__OpenBSD__)
  srandom_deterministic(seed);
#else
  srandom(seed);
#endif
}

static void usage() {
  errx(EXIT_FAILURE,
       "[OPTION] <CRONTAB EXPRESSION> <command> <arg> <...>\n"
       "version: %s (using %s mode process restriction)\n\n"
       "-f, --file <file>             lock file path (default: .runcron.lock)\n"
       "-T, --timeout <seconds>       specify command timeout (seconds)\n"
       "-P, --poll-interval <seconds> interval to retry failed command\n"
       "                                (default: 3600 seconds)\n"
       "-C, --chdir <path>            change working directory\n"
       "-n, --dryrun                  do nothing\n"
       "-p, --print                   output seconds to next timespec\n"
       "-s, --signal <signum>         signal sent task on timeout\n"
       "                                (default: 15)\n"
       "-t, --tag <string>            seed used for random intervals\n"
       "-v, --verbose                 verbose mode\n"
       "    --limit-cpu <uint32>      restrict cpu usage of cron expression\n"
       "                                parsing\n"
       "    --limit-as <uint32>       restrict memory (address space) of cron\n"
       "                                expression parsing\n"
       "    --allow-setuid-subprocess\n"
       "                              allow running unkillable tasks\n"
       "    --disable-process-restrictions\n"
       "                              do not fork cron expression processing\n"
       "    --disable-signal-on-exit\n"
       "                              disable termination of subprocesses on "
       "exit\n"
       "    --timestamp <YY-MM-DD hh-mm-ss|@epoch>\n"
       "                              set current time\n",
       RUNCRON_VERSION, RESTRICT_PROCESS);
}
