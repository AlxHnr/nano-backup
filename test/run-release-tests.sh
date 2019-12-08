#!/bin/bash -e

# Runs the test suite multiple times using valgrind, different compiler
# flags, cppcheck and clang-analyze.

test_programs=$(grep -l '^int main(' test/*.c)
test_programs=(${test_programs[@]//.c/})
test_programs=(${test_programs[@]//test\//build/test/})
build() { make -j"$(nproc)" all "${test_programs[@]}"; }

CFLAGS="-Wall -Wextra -Werror -pedantic"
CLANG_FLAGS="-Weverything -Wno-conversion -Wno-packed -Wno-padded"
CLANG_FLAGS+=" -Wno-cast-align -Wno-shadow -Wno-assign-enum"
CLANG_FLAGS+=" -Wno-switch-enum -Wno-extra-semi-stmt"
GCC_FLAGS="--all-warnings --extra-warnings -W -Wabi -Waddress \
-Waggressive-loop-optimizations -Wall -Warray-bounds -Wattributes \
-Wbad-function-cast -Wbool-compare -Wbuiltin-macro-redefined -Wcast-align \
-Wchar-subscripts -Wclobbered -Wcomment -Wcomments \
-Wcoverage-mismatch -Wdate-time -Wdeprecated -Wdeprecated-declarations \
-Wdesignated-init -Wdisabled-optimization -Wdiscarded-array-qualifiers \
-Wdiscarded-qualifiers -Wdiv-by-zero -Wdouble-promotion -Wempty-body \
-Wendif-labels -Wenum-compare -Werror-implicit-function-declaration \
-Werror=implicit-function-declaration -Wextra -Wfloat-conversion \
-Wfloat-equal -Wformat -Wformat-contains-nul -Wformat-extra-args \
-Wformat-nonliteral -Wformat-security -Wformat-signedness -Wformat-y2k \
-Wformat-zero-length -Wformat=2 -Wfree-nonheap-object -Wignored-qualifiers \
-Wimplicit -Wimplicit-function-declaration -Wimplicit-int -Wimport \
-Wincompatible-pointer-types -Winit-self -Winline -Wint-conversion \
-Wint-to-pointer-cast -Winvalid-memory-model -Winvalid-pch \
-Wjump-misses-init -Wlogical-not-parentheses -Wlogical-op -Wmain \
-Wmaybe-uninitialized -Wmemset-transposed-args -Wmissing-braces \
-Wmissing-declarations -Wmissing-field-initializers \
-Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn \
-Wmissing-parameter-type -Wmissing-prototypes -Wmultichar -Wnarrowing \
-Wnested-externs -Wnonnull -Wnormalized -Wnormalized=nfc -Wodr \
-Wold-style-declaration -Wold-style-definition -Wopenmp-simd -Woverflow \
-Woverlength-strings -Woverride-init -Wpacked-bitfield-compat \
-Wparentheses -Wpedantic -Wpointer-arith -Wpointer-sign \
-Wpointer-to-int-cast -Wpragmas -Wpsabi -Wreturn-local-addr -Wreturn-type \
-Wsequence-point -Wshift-count-negative -Wshift-count-overflow \
-Wsign-compare -Wsizeof-array-argument -Wsizeof-pointer-memaccess \
-Wstack-protector -Wstrict-aliasing -Wstrict-overflow -Wstrict-prototypes \
-Wsuggest-attribute=const -Wsuggest-attribute=format \
-Wsuggest-attribute=noreturn -Wsuggest-attribute=pure \
-Wsuggest-final-methods -Wsuggest-final-types -Wswitch -Wswitch-bool \
-Wsync-nand -Wtrampolines -Wtrigraphs -Wtype-limits -Wundef -Wuninitialized \
-Wunknown-pragmas -Wunreachable-code -Wunsafe-loop-optimizations -Wunused \
-Wunused-but-set-parameter -Wunused-but-set-variable -Wunused-function \
-Wunused-label -Wunused-local-typedefs -Wunused-macros -Wunused-parameter \
-Wunused-result -Wunused-value -Wunused-variable -Wvarargs \
-Wvariadic-macros -Wvector-operation-performance -Wvla \
-Wvolatile-register-var -Wwrite-strings -Wno-abi"

make clean
CC=gcc CFLAGS+=" $GCC_FLAGS" build
CC=gcc CFLAGS+=" $GCC_FLAGS" make test

make clean
CC=clang CFLAGS+=" $CLANG_FLAGS" build
CC=clang CFLAGS+=" $CLANG_FLAGS" make test

for sanitizer in address undefined; do
(
  make clean
  flags="-O0 -ggdb -fsanitize=$sanitizer -fno-sanitize-recover=all"
  CC=gcc CFLAGS+=" $flags" LDFLAGS="$flags" build
  CC=gcc CFLAGS+=" $flags" LDFLAGS="$flags" make test
)
done

# Run tests with valgrind.
make clean
CC=gcc CFLAGS+=" -O0 -ggdb" build

find build/ -type f -executable \
  -exec mv "{}" "{}-bin" ";" \
  -exec ln -s "$PWD/build/valgrind/run-bin.sh" "{}" ";"

mkdir -p build/valgrind
cat > build/valgrind/run-bin.sh <<EOF
#!/bin/sh -e

valgrind --leak-check=full --track-origins=yes --read-var-info=yes \\
  --log-file="$PWD/build/valgrind/log-%p" \\
  --quiet "\${0}-bin" "\$@"
EOF
chmod +x build/valgrind/run-bin.sh

make test

# Search for valgrind errors.
find build/valgrind -type f -empty -exec rm "{}" ";"
for file in build/valgrind/*; do
  test "$file" != "build/valgrind/run-bin.sh" || continue
  grep --color=auto . <<< "valgrind found errors" >&2
  exit 1;
done

make clean
CFLAGS+=" -O0 -ggdb" scan-build make -j"$(nproc)" all "${test_programs[@]}"
make clean

run_cppcheck()
{
  ! cppcheck --quiet --std=c99 --enable=all "$@" -Isrc/ -Ithird-party/ \
    -D_XOPEN_SOURCE=600 -D_FILE_OFFSET_BITS=64 -DCHAR_BIT=8 \
    --suppress="missingIncludeSystem:*" \
    --suppress="knownConditionTrueFalse:test/str.c" \
    --suppress="redundantAssignment:test/*.c" \
    src/ test/ |&
    grep --color=auto .
}

echo "Running cppcheck ..."
run_cppcheck --platform=unix32
run_cppcheck --platform=unix64

echo
echo "Success!"
