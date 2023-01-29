#!/bin/bash -e

# Runs the test suite multiple times using valgrind, different compiler
# flags, cppcheck and clang-analyze.

build() { make -j"$(nproc)" all; }

CFLAGS="-Wall -Wextra -Werror -pedantic"
CLANG_FLAGS="-Weverything -Wno-conversion -Wno-packed -Wno-padded"
CLANG_FLAGS+=" -Wno-assign-enum -Wno-switch-enum -Wno-extra-semi-stmt"
CLANG_FLAGS+=" -Wno-declaration-after-statement"
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
flags="-O0 -ggdb -fsanitize=address,undefined -fno-sanitize-recover=all"
CC=gcc CFLAGS+=" $flags" LDFLAGS="$flags" build
CC=gcc CFLAGS+=" $flags" LDFLAGS="$flags" make test

make clean
CC=clang CFLAGS+=" $CLANG_FLAGS" build
CC=clang CFLAGS+=" $CLANG_FLAGS" make test

# Run tests with valgrind.
make clean
CC=gcc CFLAGS+=" -O0 -ggdb" build
./test/run-all-tests-with-valgrind.sh

make clean
CFLAGS+=" -O0 -ggdb" scan-build make -j"$(nproc)" all
make clean

run_cppcheck()
{
  ! cppcheck --quiet --std=c99 --enable=all "$@" -Isrc/ -Ithird-party/ \
    -D_XOPEN_SOURCE=600 -D_FILE_OFFSET_BITS=64 -DCHAR_BIT=8 \
    --suppress="constParameter:*" \
    --suppress="ctunullpointer:*" \
    --suppress="ctuuninitvar:*" \
    --suppress="missingIncludeSystem:*" \
    --suppress="nullPointerRedundantCheck:*" \
    --suppress="redundantAssignment:test/*.c" \
    --suppress="unusedStructMember:src/metadata.c" \
    src/ test/ |&
    grep --color=auto .
}

echo "Running cppcheck ..."
run_cppcheck --platform=unix32
run_cppcheck --platform=unix64

echo
echo "Success!"
