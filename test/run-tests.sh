#!/bin/sh -e

export LANG=C

# Names of tests specified in the order to run.
tests="safe-wrappers memory-pool buffer path-builder file-hash colors
regex-pool string-utils string-table search-tree search repository
metadata backup backup-changes backup-filetype-changes
backup-policy-changes garbage-collector"

cd test/data/

for test in $tests; do
  rm -rf tmp/
  mkdir tmp/

  test -t 1 &&
    printf "Running \033[0;33m%s\033[0m:\n" "$test" ||
    printf "Running %s\n" "$test:"
  "../../build/test/$test"
  printf "\n"
done

rm -rf tmp/
