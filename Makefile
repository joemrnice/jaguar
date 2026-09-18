CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm -lpthread

BIN := jag

# Platform detection: exactly one reactor backend is compiled in.
# Linux uses epoll; macOS (and other BSDs) use kqueue. Native Windows
# isn't supported by either - see DESIGN_DECISIONS.md and docs/WINDOWS.md
# (use WSL, which presents as Linux and uses the epoll path).
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    EXCLUDE_BACKEND := src/runtime/reactor_epoll.c
    # ucontext.h's ucontext/makecontext/swapcontext family is deprecated
    # (but present and functional) in the macOS SDK; silence that noise
    # specifically rather than globally, and ensure it's declared at all.
    CFLAGS += -D_XOPEN_SOURCE -Wno-deprecated-declarations
else
    EXCLUDE_BACKEND := src/runtime/reactor_kqueue.c
endif

SRC := $(filter-out $(EXCLUDE_BACKEND), $(shell find src -name '*.c'))
OBJ := $(SRC:.c=.o)

.PHONY: all clean test install uninstall platform

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

test: $(BIN)
	./tests/run_tests.sh

install: $(BIN)
	./install.sh

uninstall:
	./uninstall.sh

# Prints which reactor backend this platform will build - useful for
# confirming the detection picked correctly before a full build.
platform:
	@echo "uname -s: $(UNAME_S)"
	@echo "excluded: $(EXCLUDE_BACKEND)"
