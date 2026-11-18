# Host-side tests for the game logic and the UI layer. No DS toolchain needed.
#   make test        build and run everything
#   make -C tests    from the repository root
CC     ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Wno-unused-function -I.. -fsanitize=address,undefined

all: test

test: test_rules test_ui
	./test_rules
	./test_ui

test_rules: test_rules.c ../game.h
	$(CC) $(CFLAGS) -o $@ test_rules.c

test_ui: test_ui.c ../ui.h ../game.h ../font.h
	$(CC) $(CFLAGS) -o $@ test_ui.c

clean:
	rm -f test_rules test_ui

.PHONY: all test clean
