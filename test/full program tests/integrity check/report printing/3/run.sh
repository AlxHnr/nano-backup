{
  "$NB" generated/repo/ integrity; echo "$?" > generated/exit-status
} 2>&1 | sort > generated/output

exit_status=$(cat generated/exit-status)
expected_exit_status=0
test "$exit_status" -eq "$expected_exit_status" ||
  {
    echo "wrong exit status: $exit_status (expected $expected_exit_status)"
    false
  }

diff -q generated/output generated/expected-output
