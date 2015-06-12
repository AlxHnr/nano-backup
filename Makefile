CC      ?= $(firstword $(shell which gcc clang))
CFLAGS  ?= -Wall -Wextra -Werror -pedantic -O2
CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200112L
CFLAGS  += $(shell pkg-config --cflags openssl)
LDFLAGS += $(shell pkg-config --libs openssl)

.PHONY: all doc clean
all: build/nb

-include build/dependencies.makefile
build/dependencies.makefile:
	mkdir -p build/
	$(CC) -MM src/*.c | sed -r 's,^(\S+:),build/\1,g' > $@

build/nb: $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
	$(CC) $(LDFLAGS) $^ -o $@

build/%.o:
	$(CC) $(CFLAGS) -c $< -o $@

doc:
	doxygen

clean:
	- rm -rf build/ doc/
