#!/bin/sh -e

# This script is a POSIX-compliant alternative to our custom Makefile.

cd "$(dirname "$0")/.."

mkdir -p build/
c99 -O3 -D_XOPEN_SOURCE=600 -D_FILE_OFFSET_BITS=64 \
  -I third-party/ src/*.c third-party/*/*.c -o ./build/nb
printf 'Successfully created ./build/nb\n'
