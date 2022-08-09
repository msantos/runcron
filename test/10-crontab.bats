#!/usr/bin/env bats

@test "crontab format: minutes scheduled" {
  rm -f .runcron.lock
  run runcron -np --timestamp="2018-01-24 18:18:18" "*/5 * 26 * *" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" -eq 106902 ]
}

@test "crontab format: seconds scheduled" {
  run runcron -np --timestamp="2018-01-24 18:18:18" "0 */5 * 26 * *" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" -eq 106902 ]
}

@test "crontab format: random intervals" {
  run runcron -np -t "www1.example.com" \
        --timestamp="2018-01-24 18:18:18" "0 0~8/2 * * 1~5" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]

  # The interval depends on the random(3) implementation but is constant.
  case `uname -s` in
    FreeBSD) [ "$output" -eq 380502 ]  ;;
    OpenBSD) [ "$output" -eq 474102 ]  ;;
    Linux)
      # glbc || musl
      [ "$output" -eq 474102 ] || [ "$output" -eq 34902 ] ;;
    *) skip ;;
  esac

  run runcron -np -t "www2.example.com" \
        --timestamp="2018-01-24 18:18:18" "0 0~8/2 * * 1~5" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]

  case `uname -s` in
    FreeBSD) [ "$output" -eq 553302 ] ;;
    OpenBSD) [ "$output" -eq 121302 ] ;;
    Linux)
      # glbc || musl
      [ "$output" -eq 121302 ] || [ "$output" -eq 452502 ] ;;
    *) skip ;;
  esac
}

@test "crontab format: space delimited fields" {
  run runcron -n "  *    *  * *     *" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
}

@test "crontab format: tab delimited fields" {
  run runcron -n "*	* * * * *" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
}

@test "crontab format: invalid timespec" {
  run runcron -np "* 26 * *" true
cat << EOF
$output
EOF
  [ "$status" -eq 111 ]
  [ "$output" = "runcron: error: invalid crontab timespec: Invalid number of fields, expression must consist of 6 fields" ]
}

@test "crontab format: cron_next: invalid timespec" {
  run runcron -np "3 3 31 2 *" true
cat << EOF
$output
EOF
  [ "$status" -eq 111 ]
  [ "$output" = 'runcron: error: cron_next: 3 3 31 2 *: invalid timespec' ]
}

@test "crontab alias: =daily" {
  run runcron -np --timestamp="2018-01-24 18:18:18" "=daily" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" -eq 20502 ]
}

@test "crontab alias: @monthly" {
  run runcron -np --timestamp="2021-02-09 21:24:00" -t a "@monthly" true
cat << EOF
$output
EOF
  # now[1612923840]=Tue Feb  9 21:24:00 2021
  # next[1614435577]=Sat Feb 27 09:19:37 2021
  # now[1614435577]=Sat Feb 27 09:19:37 2021
  # next[1616851177]=Sat Mar 27 09:19:37 2021

  # The interval depends on the random(3) implementation but is constant.
  case `uname -s` in
    FreeBSD) [ "$output" -eq 1124427 ]  ;;
    OpenBSD) ;&
    Linux)
      # glbc || musl
      [ "$output" -eq 1511737 ] || [ "$output" -eq 2076481 ]
      ;;
    *) skip ;;
  esac
}

@test "crontab alias: invalid alias" {
  run runcron -np "@foo" true
cat << EOF
$output
EOF
  [ "$status" -eq 111 ]
  [ "$output" = "runcron: error: invalid crontab timespec" ]
}

@test "crontab alias: timespec too long" {
  run runcron -np --timestamp="2018-01-24 18:18:18" "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 5 1 * * *" true
cat << EOF
$output
EOF
  [ "$status" -eq 111 ]
  [ "$output" = "runcron: error: timespec exceeds maximum length: 252" ]
}

@test "timestamp: daylight savings" {
  run runcron -np --timestamp "2018-03-12 1:55:00" "15 2 * * *" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" -eq 1200 ]
}

@test "timestamp: accept epoch seconds" {
  run runcron -np --timestamp "@1520834100" "15 2 * * *" true
  if [[ $output =~ runcron:\ error:\ invalid\ timestamp:\ @1520834100 ]]; then
      skip 'strptime does not support "%s"'
  fi
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" -eq 1200 ]
}

@test "crontab format: spring daylight savings" {
  run runcron -np --timestamp "2019-03-09 11:43:00" "15 11 * * *" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
}

@test "crontab format: invalid day of month" {
  run runcron -np --timestamp "2019-03-09 11:43:00" "* * * 30 2 *" true
cat << EOF
$output
EOF
  [ "$status" -eq 111 ]
}

@test "crontab format: @reboot: first run" {
  rm -f .runcron.reboot
  run runcron -f .runcron.reboot -p "@reboot" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" -eq 0 ]

  [ -f .runcron.reboot ]
  # use --dryrun or the command will sleep UINT32_MAX seconds
  run runcron -f .runcron.reboot -np "@reboot" true
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" -eq 4294967295 ]
}

@test "options: arguments allowed in command" {
  rm -f .runcron.reboot
  run runcron -f .runcron.reboot -p "@reboot" ls -al
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
}

@test "options: test changing working directory" {
  rm -f .runcron.reboot
  run runcron -C /dev -f .runcron.reboot "@reboot" ls null
cat << EOF
$output
EOF
  [ "$status" -eq 0 ]

  rm -f .runcron.reboot
  run runcron -C / -f .runcron.reboot "@reboot" ls null
cat << EOF
$output
EOF
  [ "$status" -ne 0 ]
}

@test "exit: terminate background subprocesses" {
  rm -f .runcron.reboot
  run runcron -f .runcron.reboot -p "@reboot" bash -c "exec -a RUNCRON_TEST_SLEEP sleep 60 &"
  [ "$status" -eq 0 ]
  run pgrep -f RUNCRON_TEST_SLEEP
cat << EOF
$output
EOF
  [ "$status" -eq 1 ]
}

@test "prevent unkillable (setuid) subprocesses" {
  rm -f .runcron.reboot
  case `uname -s` in
    FreeBSD) ;&
    OpenBSD) ;&
    Linux)
      run runcron -f .runcron.reboot -p "@reboot" ping -c 1 127.0.0.1
      [ "$status" -ne 0 ]
      ;;
    *) skip ;;
  esac
}

@test "task: read from stdin" {
  rm -f .runcron.reboot
  run sh -c "echo test | runcron -f .runcron.reboot '@reboot' sed 's/e/3/g'"
  cat << EOF
$output
EOF
  [ "$status" -eq 0 ]
  [ "$output" = "t3st" ]
}
