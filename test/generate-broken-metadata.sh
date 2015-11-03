#!/bin/bash

# This script generates various broken metadata files for testing. It must
# be run from the projects root directory.
set -e

target_dir="test/data/generated-broken-metadata"
dummy_path="test/data/dummy-metadata"
test_data_1="$dummy_path/test-data-1"

mkdir "$target_dir"

# Generate incomplete copies of "test-data-1".
head -c 699 "$test_data_1" > "$target_dir/missing-byte"
head -c 748 "$test_data_1" > "$target_dir/missing-slot"
head -c 393 "$test_data_1" > "$target_dir/missing-path-state-type"
head -c 703 "$test_data_1" > "$target_dir/incomplete-32-bit-value"
head -c 302 "$test_data_1" > "$target_dir/missing-32-bit-value"
head -c 3   "$test_data_1" > "$target_dir/incomplete-size"
head -c 293 "$test_data_1" > "$target_dir/missing-size"
head -c 715 "$test_data_1" > "$target_dir/incomplete-time"
head -c 310 "$test_data_1" > "$target_dir/missing-time"
head -c 180 "$test_data_1" > "$target_dir/incomplete-hash"
head -c 117 "$test_data_1" > "$target_dir/missing-hash"
head -c 281 "$test_data_1" > "$target_dir/incomplete-path"
head -c 220 "$test_data_1" > "$target_dir/missing-path"
head -c 424 "$test_data_1" > "$target_dir/incomplete-symlink-target-path"
head -c 418 "$test_data_1" > "$target_dir/missing-symlink-target-path"
head -c 756 "$test_data_1" > "$target_dir/last-byte-missing"

# Copy or generate various other broken files.
cp "$dummy_path/time-overflow" "$target_dir"
cp "$dummy_path/invalid-path-state-type" "$target_dir"
cp "$dummy_path/path-count-zero" "$target_dir"
cp "$test_data_1" "$target_dir/unneeded-trailing-bytes"
echo -n "   " >> "$target_dir/unneeded-trailing-bytes"
