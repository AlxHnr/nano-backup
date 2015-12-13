path_to_test_data="$PROJECT_PATH/test/data"

test_data_path_elements=$(echo "$path_to_test_data" | grep -o '\/' | wc -l)
expected_files=$(grep -E '^Total: \+[0-9]+ items' template/expected-output | \
  sed -r 's,^Total: \+([0-9]+) items.*$,\1,g')
total_files=$(($expected_files + $test_data_path_elements))

mkdir -p generated/repo

sed -r "s,^\/,${path_to_test_data}/,g" \
  template/config > generated/repo/config

sed -r "s,^(.. )\/(.*)$,\1${path_to_test_data}/\2,g" \
  template/expected-output | \
  sed -r "s,^Total: \+$expected_files items,Total: +$total_files items,g" | \
  sort > generated/expected-output
