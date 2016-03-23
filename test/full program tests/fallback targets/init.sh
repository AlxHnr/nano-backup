# Various helper functions.

# Generates "generated/expected-output".
#
# $1 The full or relative path to the template file.
# $2 The path which should be prepended to every found file in the output.
gen_expected_output()
{
  path_elements=$(echo "$2" | grep -o '\/' | wc -l)
  expected_files=$(grep -E '^Total: \+[0-9]+ items' "$1" | \
    sed -r 's,^Total: \+([0-9]+) items.*$,\1,g')
  total_files="0"

  test -n "$expected_files" &&
    test "$expected_files" != "0" &&
    total_files=$(($path_elements + $expected_files))

  sed -r "s,^(.. )\/(.*)$,\1$2/\2,g" "$1" | \
    sed -r "s,^Total: \+$expected_files items,Total: +$total_files items,g" | \
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
if test -f expected-output; then
  gen_expected_output "expected-output" "$PWD"
elif test -f expected-output-test-data; then
  gen_expected_output "expected-output-test-data" "$PROJECT_PATH/test/data"
fi

# Generate config file.
if test -f config; then
  gen_config_file "config" "$PWD"
elif test -f config-test-data; then
  gen_config_file "config-test-data" "$PROJECT_PATH/test/data"
fi

# Generate repository file list.
if test -f expected-repo-files; then
  sort expected-repo-files > generated/expected-repo-files
fi
