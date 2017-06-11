#!/bin/sh -e

run_clean_targets()
(
  cd "$1"

  for prefix in pre- "" post-; do
    if test -f "${prefix}clean.sh"; then
      sh -e "${prefix}clean.sh"
    elif test -f "../../fallback targets/${prefix}clean.sh"; then
      sh -e "../../fallback targets/${prefix}clean.sh"
    fi
  done
)

for test_group_path in "test/full program tests"/*; do
  test "$(basename "$test_group_path")" != "fallback targets" || continue

  for test_path in "$test_group_path"/*; do
    run_clean_targets "$test_path"
  done
done
