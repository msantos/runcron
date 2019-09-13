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

#include "limit_process.h"
#include "restrict_process.h"

#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "ccronexpr.h"

static int cronexpr_proc(runcron_t *rp, char *cronentry, unsigned int *seconds,
                         time_t now);
static int cronexpr(runcron_t *rp, char *cronentry, unsigned int *seconds,
                    time_t now);
static int fields(const char *s);
static int arg_to_timespec(const char *arg, size_t arglen, char *buf,
                           size_t buflen);
static const char *alias_to_timespec(const char *name);

static struct runcron_alias {
  const char *name;
  const char *timespec;
} runcron_aliases[] = {
    {"@yearly", "0 0 0 1 1 *"},
    {"@annually", "0 0 0 1 1 *"},
    {"@monthly", "0 0 0 1 * *"},
    {"@weekly", "0 0 0 * * 0"},
    {"@daily", "0 0 0 * * *"},
    {"@midnight", "0 0 0 * * *"},
    {"@hourly", "0 0 * * * *"},

    {"@reboot", "@reboot"},

    {NULL, NULL},
};

int cronevent(runcron_t *rp, char *cronentry, unsigned int *seconds,
              time_t now) {
  return (rp->opt & OPT_DISABLE_PROCESS_RESTRICTIONS)
             ? cronexpr(rp, cronentry, seconds, now)
             : cronexpr_proc(rp, cronentry, seconds, now);
}

static int cronexpr_proc(runcron_t *rp, char *cronentry, unsigned int *sec,
                         time_t now) {
  pid_t pid;
  int sv[2];
  unsigned int seconds;
  int status;
  int exit_value = 0;
  int n = -1;

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    return -1;

  pid = fork();

  switch (pid) {
  case -1:
    n = errno;
    (void)close(sv[0]);
    (void)close(sv[1]);
    errno = n;
    return -1;

  case 0:
    if (close(sv[0]) < 0)
      exit(111);
    if (limit_process() < 0)
      exit(111);
    if (restrict_process() < 0)
      exit(111);
    exit_value = cronexpr(rp, cronentry, &seconds, now);
    if (exit_value < 0)
      _exit(1);

    while ((n = write(sv[1], &seconds, sizeof(seconds))) == -1 &&
           errno == EINTR)
      ;

    if (n != sizeof(seconds))
      _exit(111);

    _exit(0);

  default:
    if (close(sv[1]) < 0)
      return -1;

    if (waitfor(&status) < 0)
      return -1;

    if (WIFEXITED(status))
      exit_value = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      exit_value = 128 + WTERMSIG(status);

    switch (exit_value) {
    case 0:
      break;

    case (128 + SIGXCPU):
      (void)fprintf(
          stderr, "error: cron expression parsing exceeded allotted runtime\n");
      return -1;

    case (128 + SIGSEGV):
      (void)fprintf(
          stderr,
          "error: cron expression parsing exceeded allotted memory usage\n");
      return -1;

    default:
      return -1;
    }

    while ((n = read(sv[0], &seconds, sizeof(seconds))) == -1 && errno == EINTR)
      ;

    if (n != sizeof(seconds))
      return -1;

    *sec = seconds;

    if (close(sv[0]) < 0)
      return -1;
  }

  return 0;
}

static int cronexpr(runcron_t *rp, char *cronentry, unsigned int *seconds,
                    time_t now) {
  cron_expr expr = {0};
  const char *errbuf = NULL;
  char buf[255] = {0};
  char arg[252] = {0};
  char *p;
  time_t next;
  double diff;
  int rv;

  rv = snprintf(arg, sizeof(arg), "%s", cronentry);
  if (rv < 0 || rv >= sizeof(arg)) {
    warnx("error: timespec exceeds maximum length: %zu", sizeof(arg));
    return -1;
  }

  /* replace tabs with spaces */
  for (p = arg; *p != '\0'; p++)
    if (isspace(*p))
      *p = ' ';

  if (arg_to_timespec(arg, sizeof(arg), buf, sizeof(buf)) < 0) {
    warnx("error: invalid crontab timespec");
    return -1;
  }

  if (rp->verbose > 1)
    (void)fprintf(stderr, "crontab=%s\n", buf);

  if (strcmp(buf, "@reboot") == 0) {
    *seconds = UINT32_MAX;
    return 0;
  }

  cron_parse_expr(buf, &expr, &errbuf);
  if (errbuf) {
    warnx("error: invalid crontab timespec: %s", errbuf);
    return -1;
  }

  next = cron_next(&expr, now);
  if (next == -1) {
    warnx("error: cron_next: next scheduled interval: %s",
          errno == 0 ? "invalid timespec" : strerror(errno));
    return -1;
  }

  if (rp->verbose > 0) {
    (void)fprintf(stderr, "now[%lld]=%s", (long long)now, ctime(&now));
    (void)fprintf(stderr, "next[%lld]=%s", (long long)next, ctime(&next));
  }

  diff = difftime(next, now);
  if (diff < 0) {
    warnx("error: difftime: negative duration: %.f seconds", diff);
    return -1;
  }

  *seconds = diff > UINT32_MAX ? UINT32_MAX : (unsigned int)diff;
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

static const char *alias_to_timespec(const char *name) {
  struct runcron_alias *ap;

  for (ap = runcron_aliases; ap->name != NULL; ap++) {
    if (strcmp(name, ap->name) == 0) {
      return ap->timespec;
    }
  }

  return NULL;
}
