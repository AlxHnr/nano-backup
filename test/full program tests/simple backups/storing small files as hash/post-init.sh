simple_config="$PROJECT_PATH/test/data/valid-config-files/simple.txt"

mkdir -p generated/files/subdir/
head -c 1  "$simple_config" > "generated/files/1.txt"
head -c 9  "$simple_config" > "generated/files/foo"
head -c 9  "$simple_config" > "generated/files/subdir/foo"
head -c 19 "$simple_config" > "generated/files/1.9"
head -c 20 "$simple_config" > "generated/files/subdir/20.foo"
