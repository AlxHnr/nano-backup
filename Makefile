CC      ?= $(firstword $(shell which gcc clang))
CFLAGS  ?= -Wall -Wextra -Werror -pedantic -O2
CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200112L
CFLAGS  += $(shell pkg-config --cflags openssl)
LDFLAGS += $(shell pkg-config --libs openssl)

OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
TESTS   := $(patsubst test/%.c,build/test/%,$(wildcard test/*.c))
.SECONDARY: $(TESTS:%=%.o)

.PHONY: all test doc clean
all: build/nb

-include build/dependencies.makefile
build/dependencies.makefile:
	mkdir -p build/
	$(CC) -MM src/*.c | sed -r 's,^(\S+:),build/\1,g' > $@

build/nb: $(filter-out build/test.o,$(OBJECTS))
	$(CC) $(LDFLAGS) $^ -o $@

build/%.o:
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TESTS)
	@(cd test/data/ && \
	  for test in $(TESTS); do \
	  echo "Running $$(tput setf 6)$$test$$(tput sgr0):"; \
	  "../../$$test" || exit 1; \
	  echo; \
	  done)

build/test/%: build/test/%.o \
  $(filter-out build/main.o build/error-handling.o,$(OBJECTS))
	$(CC) $(LDFLAGS) $^ -o $@

build/test/%.o: test/%.c
	 mkdir -p build/test/ && $(CC) $(CFLAGS) -Isrc/ -c $< -o $@

doc:
	doxygen

clean:
	- rm -rf build/ doc/
