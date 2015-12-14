#!/bin/sh

# Define some color codes.
unset blue
unset green
unset yellow
unset red_bold
unset normal
if [[ -t 1 ]]; then
  blue="\033[0;34m"
  green="\033[0;32m"
  yellow="\033[0;33m"
  red_bold="\033[1;31m"
  normal="\033[0m"
fi

# Prints a message, which indicates that a test is running.
#
# $1 The name of the test.
print_running_test()
{
  echo -en "  Testing $1"
  dots_to_print=$((61 - ${#1}))
  test $dots_to_print -gt 0 &&
    for i in $(seq $dots_to_print); do
      echo -n "."
    done
}

# Prints a message, which indicates that the test failed and terminates the
# program with failure. It passes all its arguments directly to `echo -e`.
fail_test()
{
  echo -e "[${red_bold}FAILURE${normal}]"
  echo -e "    ${red_bold}error${normal}: $@"
  exit 1
}

# Tries to run the given target and terminates the program on failure. If
# the target script doesn't exist, it will do nothing.
#
# $1 The name of the target.
try_to_run_target()
{
  local target_script="../../fallback targets/${1}.sh"
  test -f "${1}.sh" && local target_script="${1}.sh"

  output=$(sh -e "${target_script}" 2>&1)
  test ! $? -eq 0 &&
    fail_test "${yellow}$(basename "$target_script")${normal}: $output"
}

# Various variables available to test scripts.
export PROJECT_PATH="$PWD"
export NB="$PROJECT_PATH/build/nb"

# The main test loop.
for test_group_path in "test/full program tests/"*; do
  test_group=$(basename "$test_group_path")
  test "$test_group" == "fallback targets" && continue

  echo -e "Running full program test group: ${blue}${test_group}${normal}:"

  for test_path in "$test_group_path"/*; do
    test_name=$(basename "$test_path")
    print_running_test "$test_name"

    cd "$test_path"
    for target in clean init run clean; do
      try_to_run_target "$target";
    done
    cd - >&/dev/null

    echo -e "[${green}success${normal}]"
  done
  echo
done
