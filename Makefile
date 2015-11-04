CC      ?= $(firstword $(shell which gcc clang))
CFLAGS  ?= -Wall -Wextra -Werror -pedantic -O2
CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200112L -D_FILE_OFFSET_BITS=64
CFLAGS  += $(shell pkg-config --cflags openssl)
LDFLAGS += $(shell pkg-config --libs openssl)

OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
TESTS   := $(patsubst test/%.c,build/test/%,$(wildcard test/*.c))
.SECONDARY: $(TESTS:%=%.o)

GENERATED_CONFIGS := $(patsubst test/data/template%,test/data/generated%,\
  $(wildcard test/data/template-config-files/*))

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

test: $(TESTS) $(GENERATED_CONFIGS) test/data/test\ directory/.empty/ \
  test/data/generated-broken-metadata/
	@(cd test/data/ && \
	  for test in $(TESTS); do \
	  rm -rf tmp/; \
	  mkdir -p tmp/; \
	  echo "Running $$(tput setf 6)$$test$$(tput sgr0):"; \
	  "../../$$test" || exit 1; \
	  echo; \
	  done && \
	  rm -rf tmp/)

build/test/%: build/test/%.o \
  $(filter-out build/main.o build/error-handling.o,$(OBJECTS))
	$(CC) $(LDFLAGS) $^ -o $@

build/test/%.o: test/%.c
	 mkdir -p build/test/ && $(CC) $(CFLAGS) -Isrc/ -c $< -o $@

test/data/generated-config-files/%: test/data/template-config-files/%
	mkdir -p "$(dir $@)" && sed -r "s,^/,$$PWD/test/data/,g" "$<" > "$@"

test/data/generated-broken-metadata/:
	./test/generate-broken-metadata.sh

test/data/test\ directory/.empty/:
	mkdir -p "$@"

doc:
	doxygen

clean:
	- rm -rf build/ doc/ test/data/generated-*/ test/data/tmp/
