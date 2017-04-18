test -f "$PHASE_PATH/input" &&
  NB_INPUT=$(cat "$PHASE_PATH/input") ||
  NB_INPUT="yes"
export NB_INPUT

if test -f "$PHASE_PATH/arguments"; then
  cat "$PHASE_PATH/arguments"
else
  echo generated/repo
fi | xargs sh -c '
printf "%s" "$NB_INPUT" |
{
  "$NB" "$@"; echo "$?" > generated/exit-status;
}' -- 2>&1 | sort > generated/output

exit_status=$(cat generated/exit-status)
test -f "$PHASE_PATH/exit-status" &&
  expected_exit_status=$(cat "$PHASE_PATH/exit-status") ||
  expected_exit_status=0

test "$exit_status" -eq "$expected_exit_status" ||
  {
    echo "wrong exit status: $exit_status (expected $expected_exit_status)"
    false
  }

diff -q generated/output generated/expected-output

if test -f generated/expected-repo-files; then
  find generated/repo/ | sed 's/^generated\/repo\///g' |
  sed '/^$/d' | sort > generated/repo-files
  diff -q generated/repo-files generated/expected-repo-files
fi

# Check generated filenames inside the repository.
for path in generated/repo/?/??/*; do
  test -e "$path" || continue
  file=${path#generated/repo/}

  hash=$(printf "%s" "$file" | tr -d /)
  hash=${hash%%x*}
  real_hash=$(sha1sum "$path")
  real_hash=${real_hash%% *}
  test "$hash" = "$real_hash" ||
    {
      echo "wrong hash in filename: \"$file\" (expected $real_hash)"
      false
    }

  size=${file%x*}
  size=${size##*x}
  real_size=$(printf "%x" "$(wc -c < "$path")")
  test "$size" = "$real_size" ||
    {
      echo "wrong size in filename: \"$file\" (expected $real_size)"
      false
    }
done
