# Host (Mac/Linux) unit tests for core/battsave.c
# Usage: make -C test/host

CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Werror -I. -I../../include
SRCS = test_battsave.c fake_hw.c ../../core/battsave.c

.PHONY: test clean
test: test_battsave
	./test_battsave

test_battsave: $(SRCS) fake_hw.h ../../include/battsave.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f test_battsave
