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
for file in generated/repo/*-*-*; do
  test -e "$file" || continue

  hash=${file#*-}
  hash=${hash%-*}
  real_hash=$(sha1sum "$file")
  real_hash=${real_hash%% *}
  test "$hash" = "$real_hash" ||
    (echo "invalid hash in filename: \"$file\"" && false)

  size=${file##*-}
  real_size=$(stat --format="%s" "$file")
  test "$size" = "$real_size" ||
    (echo "invalid size in filename: \"$file\"" && false)
done
