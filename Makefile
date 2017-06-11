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

test: build/nb $(TEST_PROGRAMS) $(GENERATED_CONFIGS) \
  test/data/test\ directory/.empty/
	@(cd test/data/ && \
	  for test in safe-wrappers memory-pool buffer path-builder \
	  file-hash colors regex-pool string-utils string-table \
	  search-tree search repository metadata backup \
	  garbage-collector; do \
	  rm -rf tmp/; \
	  mkdir -p tmp/; \
	  test -t 1 && printf "Running \033[0;33m%s\033[0m:\n" "$$test" || \
	  printf "Running %s\n" "$$test:"; \
	  "../../build/test/$$test" || exit 1; \
	  printf "\n"; \
	  done && \
	  rm -rf tmp/)
	@./test/run-full-program-tests.sh

test/data/generated-config-files/%: test/data/template-config-files/%
	mkdir -p "$(dir $@)" && sed -r "s,^/,$$PWD/test/data/,g" "$<" > "$@"

test/data/test\ directory/.empty/:
	mkdir -p "$@"

clean:
	rm -rf build/ test/data/generated-*/ test/data/tmp/
	test ! -e "test/data/test directory/.empty/" || \
	  rmdir "test/data/test directory/.empty/"
	@(for test_dir in "test/full program tests"/*/*; do \
	  test -d "$$test_dir" || continue; \
	  cd "$$test_dir" && \
	  sh -e "../../fallback targets/clean.sh" || exit 1; \
	  cd - >&/dev/null; \
	  done)
