.PHONY: all clean test

PROG=   runcron
SRCS=   runcron.c \
        cronevent.c \
        ccronexpr.c \
        strtonum.c \
        waitfor.c \
        limit_process.c \
        restrict_process_null.c \
        restrict_process_rlimit.c \
        restrict_process_seccomp.c

UNAME_SYS := $(shell uname -s)
ifeq ($(UNAME_SYS), Linux)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wl,-z,relro,-z,now -Wl,-z,noexecstack
	  RESTRICT_PROCESS ?= seccomp
else ifeq ($(UNAME_SYS), OpenBSD)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wno-missing-braces -Wl,-z,relro,-z,now -Wl,-z,noexecstack
    RESTRICT_PROCESS ?= pledge
else ifeq ($(UNAME_SYS), FreeBSD)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wno-missing-braces -Wl,-z,relro,-z,now -Wl,-z,noexecstack
    RESTRICT_PROCESS ?= capsicum
else ifeq ($(UNAME_SYS), Darwin)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wno-missing-braces
endif

RM ?= rm

RESTRICT_PROCESS ?= rlimit
RUNCRON_CFLAGS ?= -g -Wall -fwrapv -pedantic

CFLAGS += $(RUNCRON_CFLAGS) \
          -DCRON_USE_LOCAL_TIME \
          -DRESTRICT_PROCESS=\"$(RESTRICT_PROCESS)\" \
          -DRESTRICT_PROCESS_$(RESTRICT_PROCESS)

LDFLAGS += $(RUNCRON_LDFLAGS)

all: $(PROG)

$(PROG):
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LDFLAGS)

clean:
	-@$(RM) $(PROG)

test: $(PROG)
	@PATH=.:$(PATH) bats test
