CC      ?= $(firstword $(shell which gcc clang))
CFLAGS  ?= -Wall -Wextra -Werror -pedantic -O2
CFLAGS  += -std=c99 -D_XOPEN_SOURCE=600 -D_FILE_OFFSET_BITS=64
CFLAGS  += $(shell pkg-config --cflags openssl)
LDFLAGS += $(shell pkg-config --libs openssl)

OBJECTS          := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
TEST_PROGRAMS    := $(shell grep -l '^int main' test/*.c)
TEST_LIB_OBJECTS := $(filter-out $(TEST_PROGRAMS),$(wildcard test/*.c))
TEST_PROGRAMS    := $(patsubst %.c,build/%,$(TEST_PROGRAMS))
TEST_LIB_OBJECTS := $(patsubst %.c,build/%.o,$(TEST_LIB_OBJECTS)) \
  $(filter-out build/nb.o build/error-handling.o,$(OBJECTS))

EMPTY_DIR         := test/data/test\ directory/.empty/
GENERATED_CONFIGS := $(patsubst test/data/template%,test/data/generated%,\
  $(wildcard test/data/template-config-files/*))

.PHONY: all test clean
all: build/nb

-include build/dependencies.makefile
build/dependencies.makefile:
	mkdir -p build/test/
	$(CC) -MM src/*.c | sed -r 's,^(\S+:),build/\1,g' > $@
	$(CC) -Isrc/ -MM test/*.c | sed -r 's,^(\S+:),build/test/\1,g' >> $@

build/nb: $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

build/%.o:
	$(CC) $(CFLAGS) -Isrc/ -c $< -o $@

build/test/%: build/test/%.o $(TEST_LIB_OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

# Workaround for Gits inability to track empty directories.
$(EMPTY_DIR):
	mkdir -p "$@"

test/data/generated-config-files/%: test/data/template-config-files/%
	mkdir -p "$(dir $@)" && sed -r "s,^/,$$PWD/test/data/,g" "$<" > "$@"

test: build/nb $(TEST_PROGRAMS) $(GENERATED_CONFIGS) $(EMPTY_DIR)
	./test/run-tests.sh && ./test/run-full-program-tests.sh

clean:
	rm -rf build/ test/data/generated-*/ test/data/tmp/
	test ! -e $(EMPTY_DIR) || rmdir $(EMPTY_DIR)
	./test/clean-full-program-tests.sh
