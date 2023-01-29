#!/bin/sh -e

# Requires `gcovr` to be installed. Everything should be build and linked
# with gcc and `-O0 -ggdb -coverage` for best results.

output="build/coverage/index.html"

cd "$(dirname "$0")/.."

test -e build/ || {
  printf "error: Project was not build\n" >&2
  exit 1
}

gcovr --delete build/
make run-test

mkdir -p build/coverage
gcovr --sort-percentage --filter "src/*" --html-details "$output"

printf 'Exported report to "%s"\n' "$output"
