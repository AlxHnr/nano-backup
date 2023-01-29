#!/bin/sh -e

# Generates compile_commands.json for use with various tools.

cd "$(dirname "$0")/.."

CC=$(make -np | grep '^CC = ' | head -n1 | sed -r 's/^CC = //')
CFLAGS=$(make -np | grep '^CFLAGS = ' | head -n1 | sed -r 's/^CFLAGS = //')

{
  printf '['
  find src/ test/ third-party/ -name '*.c' -print0 |
    xargs -0 -I {} printf \
      '{"directory":"%s","command":"%s %s -c %s -o %s.o","file":"%s"},\n' \
      "$PWD" "$CC" "$CFLAGS -Ithird-party/ -Isrc/" {} {} {} |
    sed '$ s/,$/]/'
} > compile_commands.json
