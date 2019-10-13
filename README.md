runcron - simple, safe, container-friendly cron alternative

# SYNOPSIS

runcron [*options*] *crontab expression* *command* *arg* *...*

# DESCRIPTION

`runcron` is a minimal cron running as part of a supervision tree for
automated environments. `runcron` supervises tasks:

* only allows a single instance of a job to run

* will terminate the job if the runtime exceeds the next cron interval

* periodically retries the job if it exits non-0

* if the tasks succeeds (exits 0), sleeps until the next cron interval

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

* 0: runcron will not the task and sleep indefinitely
* non-0: runcron will rerun the task after `--poll-interval` seconds
  (default: 3600)

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

-T, --timeout
: specify command timeout in seconds (-1 to disable, default: next
  cron interval)

-P, --poll-interval
: interval to retry failed commands (default: 3600s)

-n, --dryrun
: do nothing

-p, --print
: output seconds to next timespec

-v, --verbose
: verbose mode

--timestamp *YY-MM-DD hh-mm-ss|@epoch*
: provide an initial time

# BUILDING

## Quick Install

    make

    # selecting method for restricting cron expression parsing
    RESTRICT_PROCESS=seccomp make

    # using musl
    RESTRICT_PROCESS=rlimit ./musl-make

# ALTERNATIVES

* [pseudocron](https://github.com/msantos/pseudocron)

* [runwhen](http://code.dogmap.org/runwhen/)

* [supercronic](https://github.com/aptible/supercronic)

* [uschedule](https://ohse.de/uwe/uschedule.html)

# SEE ALSO

_crontab_(5)
