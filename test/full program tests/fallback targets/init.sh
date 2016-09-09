# Various helper functions.

# Generates "generated/expected-output".
#
# $1 The full or relative path to the template file.
# $2 The path which should be prepended to every found file in the output.
gen_expected_output()
{
  path_elements=$(echo "$2" | grep -o '\/' | wc -l)
  expected_files=$(grep -E '^New: [0-9]+ ' "$1" | \
    sed -r 's,^New: ([0-9]+) .*$,\1,g')
  total_files="0"

  if test -n "$expected_files"; then
    if test -f generated/repo/metadata; then
      total_files="$expected_files"
    elif test "$expected_files" != "0"; then
      total_files=$(($path_elements + $expected_files))
    fi
  fi

  sed -r "s,^(.. \^?)\/(.*)$,\1$2/\2,g" "$1" | \
    sed -r "s,^New: $expected_files ,New: $total_files ,g" | \
    sort > generated/expected-output
}

# Generates "generated/repo/config".
#
# $1 The full or relative path to the template file.
# $2 The path which should be prepended to every path in the config file.
gen_config_file()
{
  mkdir -p generated/repo
  sed -r "s,^\/,$2/,g" "$1" > generated/repo/config
}

# Initialize the test.
mkdir -p generated

# Generate expected output.
if test -f "$PHASE_PATH/expected-output"; then
  gen_expected_output "$PHASE_PATH/expected-output" "$PWD"
elif test -f "$PHASE_PATH/expected-output-test-data"; then
  gen_expected_output "$PHASE_PATH/expected-output-test-data" \
    "$PROJECT_PATH/test/data"
fi

# Generate config file.
if test -f "$PHASE_PATH/config"; then
  gen_config_file "$PHASE_PATH/config" "$PWD"
elif test -f "$PHASE_PATH/config-test-data"; then
  gen_config_file "$PHASE_PATH/config-test-data" "$PROJECT_PATH/test/data"
fi

# Generate repository file list.
if test -f "$PHASE_PATH/expected-repo-files"; then
  sort "$PHASE_PATH/expected-repo-files" > generated/expected-repo-files
fi
