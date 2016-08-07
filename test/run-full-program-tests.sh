#!/bin/sh

# Define some color codes.
unset blue
unset green
unset yellow
unset red_bold
unset normal
if test -t 1; then
  blue="[0;34m"
  green="[0;32m"
  yellow="[0;33m"
  red_bold="[1;31m"
  normal="[0m"
fi

# Prints a message, which indicates that a test is running.
#
# $1 The name of the test.
print_running_test()
{
  printf "  Testing %s" "$1"
  dots_to_print=$((61 - ${#1}))
  test $dots_to_print -gt 0 &&
    for i in $(seq $dots_to_print); do
      printf "."
    done
}

# Prints a message, which indicates that a phase is running.
#
# $1 The number of the phase.
print_running_phase()
{
  printf "    Running phase %s" "$1"
  dots_to_print=$((53 - ${#1}))
  test $dots_to_print -gt 0 &&
    for i in $(seq $dots_to_print); do
      printf "."
    done
}

# Prints a message, which indicates that the test failed and terminates the
# program with failure. It passes all its arguments directly to echo.
fail_test()
{
  echo "[${red_bold}FAILURE${normal}]"
  echo "    ${red_bold}error${normal}: $@"
  exit 1
}

# Tries to run the given target and terminates the program on failure. If
# the target script doesn't exist, it will do nothing.
#
# $1 The name of the target.
try_to_run_target()
{
  if test -f "${PHASE_PATH}/${1}.sh"; then
    target_script="${PHASE_PATH}/${1}.sh"
  elif test -f "../../fallback targets/${1}.sh"; then
    target_script="../../fallback targets/${1}.sh"
  else
    return
  fi

  output=$(sh -e "${target_script}" 2>&1)
  test ! $? -eq 0 &&
    fail_test "${yellow}$(basename "$target_script")${normal}: $output"
}

# Tries to run a target after its pre- and before its post-target.
#
# $1 The target name.
try_to_run_full_target()
{
  try_to_run_target "pre-$1"
  try_to_run_target "$1"
  try_to_run_target "post-$1"
}

# Various variables available to test scripts.
export PHASE_PATH="."
export PROJECT_PATH="$PWD"
export NB="$PROJECT_PATH/build/nb"

# The main test loop.
for test_group_path in "test/full program tests/"*; do
  test_group=$(basename "$test_group_path")
  test "$test_group" = "fallback targets" && continue

  echo "Running full program test group: ${blue}${test_group}${normal}:"

  for test_path in "$test_group_path"/*; do
    test_name=$(basename "$test_path")

    (cd "$test_path"

    if test -d 1; then
      echo "  Testing ${test_name}:"
      try_to_run_full_target clean

      for PHASE_PATH in $(seq 32); do
        test -d "$PHASE_PATH" || break

        print_running_phase "$PHASE_PATH"
        try_to_run_full_target init
        try_to_run_full_target run
        echo "[${green}success${normal}]"
      done

      try_to_run_full_target clean
      true
    else
      print_running_test "$test_name"
      for target in clean init run clean; do
        try_to_run_full_target "$target"
      done
      echo "[${green}success${normal}]"
    fi) || exit 1
  done
  echo
done
