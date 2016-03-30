#!/bin/sh -e

# This script generates various broken metadata files for testing. It must
# be run from the projects root directory.

target_dir="test/data/generated-broken-metadata"
dummy_path="test/data/dummy-metadata"
test_data_1="$dummy_path/test-data-1"

mkdir "$target_dir"

# Generate incomplete copies of "test-data-1".
head -c 667 "$test_data_1" > "$target_dir/missing-byte"
head -c 716 "$test_data_1" > "$target_dir/missing-slot"
head -c 361 "$test_data_1" > "$target_dir/missing-path-state-type"
head -c 671 "$test_data_1" > "$target_dir/incomplete-32-bit-value"
head -c 270 "$test_data_1" > "$target_dir/missing-32-bit-value"
head -c 3   "$test_data_1" > "$target_dir/incomplete-size"
head -c 261 "$test_data_1" > "$target_dir/missing-size"
head -c 683 "$test_data_1" > "$target_dir/incomplete-time"
head -c 278 "$test_data_1" > "$target_dir/missing-time"
head -c 148 "$test_data_1" > "$target_dir/incomplete-hash"
head -c 85  "$test_data_1" > "$target_dir/missing-hash"
head -c 249 "$test_data_1" > "$target_dir/incomplete-path"
head -c 188 "$test_data_1" > "$target_dir/missing-path"
head -c 392 "$test_data_1" > "$target_dir/incomplete-symlink-target-path"
head -c 386 "$test_data_1" > "$target_dir/missing-symlink-target-path"
head -c 724 "$test_data_1" > "$target_dir/last-byte-missing"

# Copy or generate various other broken files.
cp "$dummy_path/backup-id-out-of-range-"* "$target_dir"
cp "$dummy_path/invalid-path-state-type" "$target_dir"
cp "$dummy_path/path-count-zero" "$target_dir"
cp "$test_data_1" "$target_dir/unneeded-trailing-bytes"
echo -n "   " >> "$target_dir/unneeded-trailing-bytes"
