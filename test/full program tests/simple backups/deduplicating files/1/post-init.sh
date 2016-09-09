simple_search="$PROJECT_PATH/test/data/template-config-files/simple-search.txt"

mkdir -p generated/files/subdir/
head -c 21  "$simple_search" > "generated/files/first-21"
head -c 300 "$simple_search" > "generated/files/first-300"
head -c 21  "$simple_search" > "generated/files/subdir/another-21"
head -c 300 "$simple_search" > "generated/files/another-300"
head -c 300 "$simple_search" > "generated/files/subdir/300.txt"
head -c 300 "$simple_search" > "generated/files/subdir/FOO-300"
