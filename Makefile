.PHONY: all clean test

PROG=   runcron
SRCS=   runcron.c \
        cronevent.c \
        ccronexpr.c \
        strtonum.c

UNAME_SYS := $(shell uname -s)
ifeq ($(UNAME_SYS), Linux)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wl,-z,relro,-z,now -Wl,-z,noexecstack
	  RUNCRON_SANDBOX ?= seccomp
else ifeq ($(UNAME_SYS), OpenBSD)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wno-missing-braces -Wl,-z,relro,-z,now -Wl,-z,noexecstack
    RUNCRON_SANDBOX ?= pledge
else ifeq ($(UNAME_SYS), FreeBSD)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wno-missing-braces -Wl,-z,relro,-z,now -Wl,-z,noexecstack
    RUNCRON_SANDBOX ?= capsicum
else ifeq ($(UNAME_SYS), Darwin)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -pie -fPIE \
              -fno-strict-aliasing
    LDFLAGS += -Wno-missing-braces
endif

RM ?= rm

RUNCRON_SANDBOX ?= rlimit
RUNCRON_CFLAGS ?= -g -Wall -fwrapv -pedantic

CFLAGS += $(RUNCRON_CFLAGS) \
          -DCRON_USE_LOCAL_TIME \
          -DRUNCRON_SANDBOX=\"$(RUNCRON_SANDBOX)\" \
          -DRUNCRON_SANDBOX_$(RUNCRON_SANDBOX)

LDFLAGS += $(RUNCRON_LDFLAGS)

all: $(PROG)

$(PROG):
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LDFLAGS)

clean:
	-@$(RM) $(PROG)

test: $(PROG)
	@PATH=.:$(PATH) bats test
