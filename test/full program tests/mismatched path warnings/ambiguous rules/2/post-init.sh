sort > generated/expected-output <<EOF
config: line 3: regex "o 1" matches "foo 1"
config: line 5: regex "^.*$" matches "foo 1"
nb: error: ambiguous rules for path: "$PROJECT_PATH/test/data/test directory/foo 1"
EOF
