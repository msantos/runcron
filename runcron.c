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
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#define _DEFAULT_SOURCE
#include <sys/time.h>
#include <time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ccronexpr.h"
#include "runcron.h"

#define RUNCRON_VERSION "0.2.0"

#define VERBOSE(__n, ...)                                                      \
  do {                                                                         \
    if (verbose >= __n) {                                                      \
      (void)fprintf(stderr, __VA_ARGS__);                                      \
    }                                                                          \
  } while (0)

static unsigned int cronevent(char *cronentry, time_t now, int verbose);
static time_t timestamp(const char *s);
static int fields(const char *s);
static int arg_to_timespec(const char *arg, size_t arglen, char *buf,
                           size_t buflen);
static const char *alias_to_timespec(const char *alias);
static void usage();

static int read_exit_status(int fd, int *status);
static int write_exit_status(int fd, int status);
void sleepfor(unsigned int seconds);
int waitfor(int *status);
int signal_init();
void signal_handler(int sig);
static void usage();

extern char *__progname;

static struct runcron_alias {
  const char *name;
  const char *timespec;
} runcron_aliases[] = {{"@yearly", "0 0 0 1 1 *"},
                       {"@annually", "0 0 0 1 1 *"},
                       {"@monthly", "0 0 0 1 * *"},
                       {"@weekly", "0 0 0 * * 0"},
                       {"@daily", "0 0 0 * * *"},
                       {"@midnight", "0 0 0 * * *"},
                       {"@hourly", "0 0 * * * *"},

                       {"@reboot", "@reboot"},

                       {NULL, NULL}};

enum {
  OPT_TIMESTAMP = 1,
  OPT_PRINT = 2,
  OPT_DRYRUN = 4,
};

static const struct option long_options[] = {
    {"file", required_argument, NULL, 'f'},
    {"timeout", required_argument, NULL, 'T'},
    {"poll-interval", required_argument, NULL, 'P'},
    {"dryrun", no_argument, NULL, 'n'},
    {"print", no_argument, NULL, 'p'},
    {"timestamp", required_argument, NULL, OPT_TIMESTAMP},
    {"verbose", no_argument, NULL, 'v'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

pid_t pid;
int default_signal = SIGTERM;

int main(int argc, char *argv[]) {
  char *file = ".runcron.lock";
  char *cronentry;
  int fd;
  int status = 0;
  time_t now;
  unsigned int seconds;
  int32_t timeout = 0;
  int32_t poll_interval = 3600; /* 1 hour */
  const char *errstr = NULL;
  int exit_value = 0;

  int opt = 0;
  int verbose = 0;
  int ch;

  now = time(NULL);
  if (now == -1)
    err(EXIT_FAILURE, "time");

  (void)localtime(&now);

  while ((ch = getopt_long(argc, argv, "f:hnpP:T:v", long_options, NULL)) !=
         -1) {
    switch (ch) {
    case 'f':
      file = optarg;
      break;

    case 'n':
      opt |= OPT_DRYRUN;
      break;

    case 'p':
      opt |= OPT_PRINT;
      break;

    case 'P':
      poll_interval = strtonum(optarg, 0, INT_MAX, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case 'T':
      timeout = strtonum(optarg, INT_MIN, INT_MAX, &errstr);
      if (errno)
        err(EXIT_FAILURE, "strtonum: %s: %s", optarg, errstr);
      break;

    case 'v':
      verbose++;
      break;

    case OPT_TIMESTAMP:
      now = timestamp(optarg);
      if (now == -1)
        errx(EXIT_FAILURE, "error: invalid timestamp: %s", optarg);
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

  seconds = cronevent(cronentry, now, verbose);

  fd = open(file, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);

  if (fd < 0) {
    switch (errno) {
    case EEXIST:
      fd = open(file, O_RDWR | O_CLOEXEC, 0);
      if (fd < 0)
        err(111, "open");
      if (read_exit_status(fd, &status) < 0)
        err(111, "read_exit_status");
      break;
    default:
      err(111, "open");
    }
  } else {
    if (seconds == UINT32_MAX) {
      status = 1;
      seconds = 0;
    }
    if (write_exit_status(fd, status) < 0)
      err(111, "write_exit_status");
  }

  if (!(opt & OPT_DRYRUN) && (flock(fd, LOCK_EX | LOCK_NB) < 0))
    exit(111);

  if (status != 0) {
    if (seconds > poll_interval) {
      seconds = poll_interval;
    }
  }

  if (opt & OPT_PRINT)
    (void)printf("%lu\n", (long unsigned int)seconds);

  if (timeout == 0) {
    timeout = cronevent(cronentry, now + seconds, verbose);
  }

  VERBOSE(1, "last exit status was %d, sleep interval is %ds, command timeout "
             "is %ds\n",
          status, seconds, timeout);

  if (opt & OPT_DRYRUN)
    exit(0);

  sleepfor(seconds);

  pid = fork();

  switch (pid) {
  case -1:
    exit(111);
  case 0:
    if (setpgid(0, 0) < 0)
      err(111, "setpgid");

    (void)execvp(argv[0], argv);
    exit(111);
  default:
    if (signal_init() < 0) {
      (void)kill(pid * -1, default_signal);
    }
    VERBOSE(1, "running command: timeout is set to %ds\n", timeout);
    if (timeout > 0) {
      alarm(timeout);
    }
    if (waitfor(&status) < 0) {
      (void)kill(pid * -1, default_signal);
      exit(111);
    }
  }

  if (WIFEXITED(status))
    exit_value = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    exit_value = 128 + WTERMSIG(status);

  VERBOSE(3, "status=%d exit_value=%d\n", status, exit_value);

  if (write_exit_status(fd, exit_value) < 0)
    err(111, "write_exit_status");

  exit(exit_value);
}

static unsigned int cronevent(char *cronentry, time_t now, int verbose) {
  cron_expr expr = {0};
  const char *errbuf = NULL;
  char buf[255] = {0};
  char arg[252] = {0};
  char *p;
  time_t next;
  double diff;
  int rv;

  rv = snprintf(arg, sizeof(arg), "%s", cronentry);
  if (rv < 0 || rv >= sizeof(arg))
    errx(EXIT_FAILURE, "error: timespec exceeds maximum length: %zu",
         sizeof(arg));

  /* replace tabs with spaces */
  for (p = arg; *p != '\0'; p++)
    if (isspace(*p))
      *p = ' ';

  if (arg_to_timespec(arg, sizeof(arg), buf, sizeof(buf)) < 0)
    errx(EXIT_FAILURE, "error: invalid crontab timespec");

  if (verbose > 1)
    (void)fprintf(stderr, "crontab=%s\n", buf);

  if (strcmp(buf, "@reboot") == 0) {
    return UINT32_MAX;
  }

  cron_parse_expr(buf, &expr, &errbuf);
  if (errbuf)
    errx(EXIT_FAILURE, "error: invalid crontab timespec: %s", errbuf);

  next = cron_next(&expr, now);
  if (next == -1)
    errx(EXIT_FAILURE, "error: cron_next: next scheduled interval: %s",
         errno == 0 ? "invalid timespec" : strerror(errno));

  if (verbose > 0) {
    (void)fprintf(stderr, "now[%lld]=%s", (long long)now, ctime(&now));
    (void)fprintf(stderr, "next[%lld]=%s", (long long)next, ctime(&next));
  }

  diff = difftime(next, now);
  if (diff < 0)
    errx(EXIT_FAILURE, "error: difftime: negative duration: %.f seconds", diff);

  return diff > UINT32_MAX ? UINT32_MAX : (unsigned int)diff;
}

void sleepfor(unsigned int seconds) {
  while (seconds > 0)
    seconds = sleep(seconds);
}

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

void signal_handler(int sig) {
  if (pid > 0)
    (void)kill(pid * -1, sig == SIGALRM ? default_signal : sig);
}

int signal_init() {
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

static int fields(const char *s) {
  int n = 0;
  const char *p = s;
  int field = 0;

  for (; *p != '\0'; p++) {
    if (*p != ' ') {
      if (!field) {
        n++;
        field = 1;
      }
    } else {
      field = 0;
    }
  }

  return n;
}

static int arg_to_timespec(const char *arg, size_t arglen, char *buf,
                           size_t buflen) {
  const char *timespec;
  int n;
  int rv;

  n = fields(arg);

  switch (n) {
  case 1:
    timespec = alias_to_timespec(arg);

    if (timespec == NULL)
      return -1;

    rv = snprintf(buf, buflen, "%s", timespec);
    break;

  case 5:
    rv = snprintf(buf, buflen, "0 %s", arg);
    break;

  case 6:
  default:
    rv = snprintf(buf, buflen, "%s", arg);
    break;
  }

  return (rv < 0 || rv >= buflen) ? -1 : 0;
}

static time_t timestamp(const char *s) {
  struct tm tm = {0};

  switch (s[0]) {
  case '@':
    if (strptime(s + 1, "%s", &tm) == NULL)
      return -1;

    break;

  default:
    if (strptime(s, "%Y-%m-%d %T", &tm) == NULL)
      return -1;

    break;
  }

  tm.tm_isdst = -1;

  return mktime(&tm);
}

static const char *alias_to_timespec(const char *name) {
  struct runcron_alias *ap;

  for (ap = runcron_aliases; ap->name != NULL; ap++) {
    if (strcmp(name, ap->name) == 0) {
      return ap->timespec;
    }
  }

  return NULL;
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
       "version: %s\n\n"
       "-f, --file             lock file path (default: .runcron.lock)\n"
       "-T, --timeout          specify command timeout (seconds)\n"
       "-P, --poll-interval    interval to retry failed commands (default: "
       "3600s)\n"
       "-n, --dryrun           do nothing\n"
       "-p, --print            output seconds to next timespec\n"
       "-v, --verbose          verbose mode\n"
       "    --timestamp <YY-MM-DD hh-mm-ss|@epoch>\n"
       "                       provide an initial time\n",
       RUNCRON_VERSION);
}
