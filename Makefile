CFLAGS           += -std=c99 -D_XOPEN_SOURCE=600 -D_FILE_OFFSET_BITS=64
OBJECTS          := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
OBJECTS          += build/third-party/BLAKE2/blake2b.o
OBJECTS          += build/third-party/SipHash/siphash.o
OBJECTS          += $(patsubst third-party/CRegion/%.c,build/third-party/CRegion/%.o,\
  $(wildcard third-party/CRegion/*.c))
TEST_PROGRAMS    := $(shell grep -l '^int main' test/*.c)
TEST_LIB_OBJECTS := $(filter-out $(TEST_PROGRAMS),$(wildcard test/*.c))
TEST_PROGRAMS    := $(patsubst %.c,build/%,$(TEST_PROGRAMS))
TEST_LIB_OBJECTS := $(patsubst %.c,build/%.o,$(TEST_LIB_OBJECTS)) \
  $(filter-out build/nb.o build/error-handling.o,$(OBJECTS))

EMPTY_DIR         := test/data/test\ directory/.empty/
GENERATED_CONFIGS := $(patsubst test/data/template%,test/data/generated%,\
  $(wildcard test/data/template-config-files/*))

build/nb: $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

.PHONY: all test run-tests clean
all: build/nb $(TEST_PROGRAMS) $(GENERATED_CONFIGS) $(EMPTY_DIR)

-include build/dependencies.makefile
build/dependencies.makefile:
	mkdir -p build/test/ build/third-party/
	$(CC) -MM -Ithird-party/ src/*.c | sed -r 's,^(\S+:),build/\1,g' > $@
	$(CC) -MM -Ithird-party/ -Isrc/ test/*.c | sed -r 's,^(\S+:),build/test/\1,g' >> $@
	{ printf "["; \
	  find src/ test/ third-party/ -name "*.c" -print0 | xargs -0 -I {} \
	    printf '{"directory":"%s","command":"%s %s -c %s -o %s.o","file":"%s"},\n' \
	    "$$PWD" "$(CC)" "$(CFLAGS) -Ithird-party/ -Isrc/" {} {} {} | sed '$$ s/,$$/]/'; \
	} > compile_commands.json

build/third-party/BLAKE2/%.o: third-party/BLAKE2/%.c
	mkdir -p build/third-party/BLAKE2
	$(CC) $(CFLAGS) -O3 -c $< -o $@

build/third-party/CRegion/%.o: third-party/CRegion/%.c
	mkdir -p build/third-party/CRegion
	$(CC) $(CFLAGS) -c $< -o $@

build/third-party/SipHash/%.o: third-party/SipHash/%.c
	mkdir -p build/third-party/SipHash
	$(CC) $(CFLAGS) -O3 -c $< -o $@

build/%.o:
	$(CC) $(CFLAGS) -Isrc/ -Ithird-party/ -c $< -o $@

build/test/%: build/test/%.o $(TEST_LIB_OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

# Workaround for Gits inability to track empty directories.
$(EMPTY_DIR):
	mkdir -p "$@"

test/data/generated-config-files/%: test/data/template-config-files/%
	mkdir -p "$(dir $@)" && sed -r "s,^/,$$PWD/test/data/,g" "$<" > "$@"

test: all
	@$(MAKE) --no-print-directory run-test
run-test:
	@./test/run-tests.sh && ./test/run-full-program-tests.sh

clean:
	rm -rf build/ compile_commands.json test/data/generated-*/ test/data/tmp/
	test ! -e $(EMPTY_DIR) || rmdir $(EMPTY_DIR)
	./test/clean-full-program-tests.sh
