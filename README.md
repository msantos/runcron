runcron - simple, safe, container-friendly cron alternative

# SYNOPSIS

runcron [*options*] *crontab expression* *command* *arg* *...*

# DESCRIPTION

`runcron` is a minimal cron running as part of a process supervision
tree for automated environments. runcron is intended to be simple,
safe and container-friendly.

`runcron` runs under a supervisor like
[daemontools](https://cr.yp.to/daemontools.html) and exits after task
completion. The supervisor restarts runcron, taking any action based on
the task exit status:

    svscan,17276,17276 service/
      |-supervise,17277,17276 date17
      |   `-runcron,17308,17276 */17 * * * * * sh -c echo 17: $(date)
      |-supervise,17279,17276 date33
      |   `-runcron,17303,17276 */33 * * * * * sh -c echo 33: $(date)
      `-supervise,17280,17276 sleep
          `-runcron,17282,17276 @reboot sleep inf
              `-sleep,17288,17288 inf

`runcron` supervises tasks:

* only allows a single instance of a job to run

* job runtime is limited to the next cron interval

* when the task is complete, exits with value set to the task exit status

* periodically retries the job if it exits non-0

* if the tasks succeeds (exits 0), when restarted, sleeps until the next
  cron interval

* terminates any background subprocesses when the foreground process exits

* attempts to prevent running unkillable (setuid) subprocesses

cron expressions are parsed using
[ccronexpr](https://github.com/staticlibs/ccronexpr).

The standard crontab(5) expressions are supported. The seconds field
is optional:

				field          allowed values
				-----          --------------
				second         0-59 (optional)
				minute         0-59
				hour           0-23
				day of month   1-31
				month          1-12 (or names, see below)
				day of week    0-7 (0 or 7 is Sun, or use names)

crontab(5) aliases also work:

				string         meaning
				------         -------
				@reboot        Run once, at startup (see below).
				@yearly        Run once a year, "0 0 1 1 *".
				@annually      (same as @yearly)
				@monthly       Run once a month, "0 0 1 * *".
				@weekly        Run once a week, "0 0 * * 0".
				@daily         Run once a day, "0 0 * * *".
				@midnight      (same as @daily)
				@hourly        Run once an hour, "0 * * * *".

## @reboot

The `@reboot` alias runs the task immediately. The behaviour of subsequent
attempts to run the task depends on the exit status of the previous run:

* 0: runcron will not run the task and sleep indefinitely
* non-0: runcron will rerun the task after `--poll-interval` seconds
  (default: 3600)

Since the runcron state is written to a file (see `-f` option), the
state can persist between reboots.

~~~
umask 077
mkdir -p /tmp/reboot
runcron -f /tmp/reboot/runcron.lock ...
~~~

# EXAMPLES

        # Attempt to connect to google daily
        # If the connection fails, the task will be retried hourly.
        runcron "43 7 * * *" nc -z google.com 80

        # Run at 9:03am on the 20th of each month.
        # After the first run, the job will be terminated before the
        # next scheduled run.
        runcron "3 9 20 * *" sleep inf

## daemontools run script

        #!/bin/sh

        # Run daily at 8:15am
        exec runcron "15 8 * * *" echo Running job

# OPTIONS

-f, --file
: lock file path (default: .runcron.lock)

-C, --chdir
: change working directory before running command

-T, --timeout
: specify command timeout in seconds (-1 to disable, default: next
  cron interval)

-P, --poll-interval
: interval to retry failed commands (default: 3600s)

-n, --dryrun
: do nothing

-p, --print
: output seconds to next timespec

-s, --signal
: signal sent on command timeout

  The signal is also sent on job completion to clean up any background
  tasks (use --disable-signal-on-exit to disable).

  Default: 15 (SIGTERM)

-v, --verbose
: verbose mode

--timestamp *YY-MM-DD hh-mm-ss|@epoch*
: provide an initial time

--limit-cpu
: restrict cpu usage of cron expression parsing (default: 10 seconds)

--limit-as
: restrict memory (address space) of cron expression parsing (default: 1 Mb)

--allow-setuid-subprocess
: allow running potentially unkillable subprocesses

--disable-process-restrictions
: do not fork cron expression processing

--disable-signal-on-exit
: By default, any background subprocesses are terminated when the
  foreground process is terminated. Use this option to disable signalling
  background jobs on exit.

# SIGNALS

## Before exec(3)

Before the task is `exec(3)`ed, signals sent to runcron are ignored
except for:

SIGUSR1
: Run the job immediately

SIGUSR2
: Print the remaining number of seconds to stderr

SIGINT
: Exit with status 130 (128+SIGINT)

## After exec(3)

Signals (excluding SIGKILL) sent to runcron when the task is running
are forwarded to the task process group.

# BUILDING

## Quick Install

    make

    # selecting method for restricting cron expression parsing
    RESTRICT_PROCESS=seccomp make

    #### using musl
    RESTRICT_PROCESS=rlimit ./musl-make

    ## linux seccomp sandbox: requires kernel headers

    # clone the kernel headers somewhere
    cd /path/to/dir
    git clone https://github.com/sabotage-linux/kernel-headers.git

    # then compile
    RUNCRON_INCLUDE=/path/to/dir ./musl-make clean all

# ALTERNATIVES

* [pseudocron](https://github.com/msantos/pseudocron)

* [snooze](https://github.com/leahneukirchen/snooze)

* [runwhen](http://code.dogmap.org/runwhen/)

* [supercronic](https://github.com/aptible/supercronic)

* [uschedule](https://ohse.de/uwe/uschedule.html)

# SEE ALSO

_crontab_(5)
