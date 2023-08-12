#!/bin/sh -e

cd "$(dirname "$0")/.."

{
  find src/ -type f -name '*.[ch]' -print0
  find test/ -type f -name '*.[ch]' -print0
} | xargs -0 clang-format --dry-run -Werror
