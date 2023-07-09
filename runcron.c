/* Copyright (c) 2019-2023, Michael Santos <michael.santos@gmail.com>
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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "cronevent.h"
#include "fnv1a.h"
#include "restrict_process.h"
#include "timestamp.h"
#include "waitfor.h"
#ifndef HAVE_STRTONUM
#include "strtonum.h"
#endif
#ifndef HAVE_SETPROCTITLE
#include "setproctitle.h"
#endif

#ifdef RESTRICT_PROCESS_capsicum
#include <sys/procdesc.h>
#endif

#ifdef HAVE_SETPROCTITLE
#define RUNCRON_TITLE "(%s %ds) %s"
#else
#define RUNCRON_TITLE "runcron: (%s %ds) %s"
#endif

#define RUNCRON_VERSION "0.19.2"

static int open_exit_status(char *file, int *status);
static int read_exit_status(int fd, int *status);
static int write_exit_status(int fd, int status);
void sleepfor(unsigned int seconds);
int signal_init(void (*handler)(int, siginfo_t *, void *));
void sa_handler_sleep(int sig, siginfo_t *info, void *context);
void sa_handler_wait(int sig, siginfo_t *info, void *context);
static int set_env(char *key, int val);
static void print_argv(int argc, char *argv[]);
static int randinit(char *tag);
static char *join(char **arg, size_t n);
static void usage(void);

static const struct option long_options[] = {
    {"file", required_argument, NULL, 'f'},
    {"chdir", required_argument, NULL, 'C'},
    {"tag", required_argument, NULL, 't'},
    {"timeout", required_argument, NULL, 'T'},
    {"retry-interval", required_argument, NULL, 'R'},
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
    {"version", no_argument, NULL, 'V'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

static pid_t pid;
static int fdp = -1;
static int default_signal = SIGTERM;
static volatile sig_atomic_t runnow = 0;
static volatile sig_atomic_t remaining = 0;

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
  unsigned int retry_interval = 3600; /* 1 hour */
  const char *errstr;
  int exit_value = 0;
  int signal_on_exit = 1;
  int allow_setuid_subprocess = 0;

  char **oargv = argv + 1;
  int oargc = argc - 1;
  char *procname;

  int ch;

#ifndef HAVE_SETPROCTITLE
  spt_init(argc, argv);
#endif

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

  while ((ch = getopt_long(argc, argv, "+C:f:hnpP:R:s:t:T:vV", long_options,
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

    case 'P': /* deprecated: use -R */
    case 'R':
      errno = 0;
      retry_interval = strtonum(optarg, 0, INT_MAX, &errstr);
      if (errno)
        err(2, "strtonum: %s: %s", optarg, errstr);
      break;

    case 's':
      errno = 0;
      default_signal = strtonum(optarg, 0, NSIG, &errstr);
      if (errno)
        err(2, "strtonum: %s: %s", optarg, errstr);
      break;

    case 't':
      tag = optarg;
      break;

    case 'T':
      errno = 0;
      timeout = strtonum(optarg, -1, UINT32_MAX, &errstr);
      if (errno)
        err(2, "strtonum: %s: %s", optarg, errstr);
      break;

    case 'v':
      rp->verbose++;
      break;

    case 'V':
      (void)printf("%s\n", RUNCRON_VERSION);
      exit(EXIT_SUCCESS);
      break;

    case OPT_ALLOW_SETUID_SUBPROCESS:
      allow_setuid_subprocess = 1;
      break;

    case OPT_LIMIT_CPU:
      errno = 0;
      rp->cpu = strtonum(optarg, -1, UINT32_MAX, &errstr);
      if (errno)
        err(2, "strtonum: %s: %s", optarg, errstr);
      break;

    case OPT_LIMIT_AS:
      errno = 0;
      rp->as = strtonum(optarg, -1, UINT32_MAX, &errstr);
      if (errno)
        err(2, "strtonum: %s: %s", optarg, errstr);
      break;

    case OPT_TIMESTAMP:
      now = timestamp(optarg);
      if (now == -1)
        errx(2, "error: invalid timestamp: %s", optarg);
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
      exit(0);
    }
  }

  argc -= optind;
  argv += optind;

  if (argc < 2) {
    usage();
    exit(2);
  }

  cronentry = argv[0];

  argc--;
  argv++;

  procname = join(oargv, oargc);
  if (procname == NULL)
    err(111, "join");

  if (randinit(tag) < 0)
    err(111, "randinit");

  if (!allow_setuid_subprocess && disable_setuid_subprocess() < 0)
    err(111, "disable_setuid_subprocess");

  if (cronevent(rp, cronentry, &seconds, now) < 0)
    exit(111);

  /* @reboot:if the runcron state file doesn't exit, set the exit status
   * to 255. */
  if (seconds == UINT32_MAX)
    status = 255;

  fd = open_exit_status(file, &status);
  if (fd < 0)
    err(111, "open_exit_status: %s", file);

  /* @reboot: run immediately */
  if (seconds == UINT32_MAX && status == 255)
    seconds = 0;

  if (!(rp->opt & OPT_DRYRUN) && (flock(fd, LOCK_EX | LOCK_NB) < 0))
    err(111, "flock");

  if ((cwd != NULL) && (chdir(cwd) < 0)) {
    err(111, "chdir: %s", cwd);
  }

  if (status != 0) {
    if (seconds > retry_interval) {
      seconds = retry_interval;
    }
  }

  if (rp->opt & OPT_PRINT)
    (void)printf("%lu\n", (long unsigned int)seconds);

  if (timeout == 0) {
    if (cronevent(rp, cronentry, &timeout, now + seconds) < 0)
      exit(111);
  }

  if ((set_env("RUNCRON_TIMEOUT", timeout) < 0) ||
      (set_env("RUNCRON_EXITSTATUS", status) < 0))
    err(111, "set_env");

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

  setproctitle(RUNCRON_TITLE, status == 0 ? "sleep" : "retry", seconds,
               procname);

  sleepfor(seconds);

  if (status == 0) {
    if (write_exit_status(fd, 128 + SIGKILL) < 0)
      err(111, "write_exit_status: %s", file);
  }

#ifdef RESTRICT_PROCESS_capsicum
  pid = pdfork(&fdp, PD_CLOEXEC);
#else
  pid = fork();
#endif

  switch (pid) {
  case -1:
    err(111, "fork");
  case 0:
    if (setsid() < 0)
      err(111, "setsid");

    if (restrict_process_signal_on_supervisor_exit() < 0)
      err(111, "restrict_process_signal_on_supervisor_exit");

    (void)execvp(argv[0], argv);
    exit(127);
  default:
    if (restrict_process_wait(fdp) < 0) {
      err(111, "restrict_process_wait");
    }

    if (signal_init(sa_handler_wait) < 0) {
#ifdef RESTRICT_PROCESS_capsicum
      (void)pdkill(fdp, default_signal);
#else
      (void)kill(-pid, default_signal);
#endif
    }
    if (rp->verbose >= 1) {
      print_argv(argc, argv);
      (void)fprintf(stderr, ": running command: timeout is set to %us\n",
                    timeout);
    }
    if (timeout < UINT32_MAX) {
      alarm(timeout);
    }
    setproctitle(RUNCRON_TITLE, "running", timeout, procname);
    if (waitfor(fdp, &status) < 0) {
      warn("waitfor");
#ifdef RESTRICT_PROCESS_capsicum
      (void)pdkill(fdp, default_signal);
#else
      (void)kill(-pid, default_signal);
#endif
      exit(111);
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
#ifdef RESTRICT_PROCESS_capsicum
    (void)pdkill(fdp, default_signal);
#else
    (void)kill(-pid, default_signal);
#endif
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

void sa_handler_sleep(int sig, siginfo_t *info, void *context) {
  switch (sig) {
  case SIGUSR1:
  case SIGALRM:
    runnow = 1;
    break;
  case SIGUSR2:
    remaining = 1;
    break;
  case SIGINT:
    _exit(111);
  case SIGTERM:
    _exit(111);
  default:
    break;
  }
}

void sa_handler_wait(int sig, siginfo_t *info, void *context) {
  switch (sig) {
  case SIGUSR1:
  case SIGUSR2:
    break;
  case SIGALRM:
    /* ignore SIGALRM generated by kill(2), sigqueue(3) */
    if (info->si_pid != 0) {
      return;
    }
    /* fallthrough */
  default:
    if (pid > 0)
#ifdef RESTRICT_PROCESS_capsicum
      (void)pdkill(fdp, sig == SIGALRM ? default_signal : sig);
#else
      (void)kill(-pid, sig == SIGALRM ? default_signal : sig);
#endif
  }
}

int signal_init(void (*handler)(int, siginfo_t *, void *)) {
  struct sigaction act = {0};
  int sig;

  act.sa_flags |= SA_SIGINFO;
  act.sa_sigaction = handler;
  (void)sigfillset(&act.sa_mask);

  for (sig = 1; sig < NSIG; sig++) {
    switch (sig) {
    case SIGCHLD:
      continue;
    default:
      break;
    }

    if (sigaction(sig, &act, NULL) < 0) {
      if (errno == EINVAL)
        continue;

      return -1;
    }
  }

  return 0;
}

static int open_exit_status(char *file, int *status) {
  int fd;

  fd = open(file, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);

  if (fd < 0) {
    switch (errno) {
    case EEXIST:
      fd = open(file, O_RDWR | O_CLOEXEC, 0);
      if (fd < 0)
        return -1;
      if (read_exit_status(fd, status) < 0) {
        (void)close(fd);
        return -1;
      }
      return fd;
    default:
      return -1;
    }
  }

  if (write_exit_status(fd, *status) < 0) {
    (void)close(fd);
    return -1;
  }

  return fd;
}

static int write_exit_status(int fd, int status) {
  unsigned char buf;

  buf = status > 255 ? 128 : (unsigned char)status;

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

static int set_env(char *key, int val) {
  char str[11];
  int rv;

  rv = snprintf(str, sizeof(str), "%u", val);
  if (rv < 0 || (unsigned)rv >= sizeof(str))
    return -1;

  if ((setenv(key, str, 1) < 0))
    return -1;

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

static int randinit(char *tag) {
  uint32_t seed;
  char name[MAXHOSTNAMELEN] = {0};
  size_t len;

  if (tag == NULL) {
    if (gethostname(name, sizeof(name) - 1) < 0)
      return -1;
    tag = name;
  }

  len = strlen(tag);

  if (len == 0) {
    struct timeval tv = {0};
    if (gettimeofday(&tv, NULL) < 0)
      return -1;
    seed = getpid() ^ tv.tv_sec ^ tv.tv_usec;
  } else {
    seed = fnv1a((uint8_t *)tag, len);
  }

#if defined(__OpenBSD__)
  srandom_deterministic(seed);
#else
  srandom(seed);
#endif
  return 0;
}

static char *join(char **arg, size_t n) {
  size_t len = 0;
  size_t alen = 0;
  char *buf;
  size_t i;
  int append = 0;
  char *space = " ";

  if (n == 0) {
    errno = EINVAL;
    return NULL;
  }

  for (i = 0; i < n; i++) {
    len += strlen(arg[i]);
  }

  len += n - 1; /* spaces */
  buf = calloc(len + 1, 1);
  if (buf == NULL)
    return NULL;

  for (i = 0; i < n; i++) {
    size_t argsz;

    if (append) {
      if (alen + 1 > len)
        goto ERR;
      (void)memcpy(buf + alen, space, 1);
      alen += 1;
    }
    argsz = strlen(arg[i]);
    if (alen + argsz > len)
      goto ERR;
    (void)memcpy(buf + alen, arg[i], argsz);
    alen += argsz;
    append = 1;
  }

  return buf;

ERR:
  free(buf);
  errno = EINVAL;
  return NULL;
}

static void usage() {
  (void)fprintf(
      stderr,
      "[OPTION] <CRONTAB EXPRESSION> <command> <arg> <...>\n"
      "version: %s (using %s mode process restriction)\n\n"
      "-f, --file <file>              lock file path (default: .runcron.lock)\n"
      "-T, --timeout <seconds>        specify command timeout\n"
      "-R, --retry-interval <seconds> retry failed command (default: 3600)\n"
      "-C, --chdir <path>             change working directory\n"
      "-n, --dryrun                   do nothing\n"
      "-p, --print                    output seconds to next timespec\n"
      "-s, --signal <signum>          signal sent task on timeout (default: "
      "15)\n"
      "-t, --tag <string>             seed used for random intervals\n"
      "-v, --verbose                  verbose mode\n"
      "-V, --version                  runcron version\n"
      "    --limit-cpu <uint32>       restrict cpu usage of cron expression\n"
      "                                 parsing\n"
      "    --limit-as <uint32>        restrict memory (address space) of cron\n"
      "                                 expression parsing\n"
      "    --allow-setuid-subprocess  allow running unkillable tasks\n"
      "    --disable-process-restrictions\n"
      "                               do not fork cron expression processing\n"
      "    --disable-signal-on-exit   disable termination of subprocesses on "
      "exit\n"
      "    --timestamp <YY-MM-DD hh-mm-ss|@epoch>\n"
      "                               set current time\n",
      RUNCRON_VERSION, RESTRICT_PROCESS);
}
