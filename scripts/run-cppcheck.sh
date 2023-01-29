#!/bin/sh -e

# This script runs cppcheck with suppressed false-positives.

cd "$(dirname "$0")/.."

cppcheck --quiet --std=c99 --enable=all --error-exitcode=1 \
  --platform=unix64 -Isrc/ -Ithird-party/ -D_XOPEN_SOURCE=600 \
  -D_FILE_OFFSET_BITS=64 -DCHAR_BIT=8 \
  --suppress="constParameter:*" \
  --suppress="ctunullpointer:*" \
  --suppress="ctuuninitvar:*" \
  --suppress="missingIncludeSystem:*" \
  --suppress="nullPointerRedundantCheck:*" \
  --suppress="redundantAssignment:test/*.c" \
  --suppress="unusedStructMember:src/metadata.c" \
  src/ test/
