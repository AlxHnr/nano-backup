sort > generated/expected-output <<EOF
config: line 2: string ".hidden 2" matches ".hidden 2"
config: line 7: regex "^.hidden" matches ".hidden 2"
nb: error: ambiguous rules for path: "$PROJECT_PATH/test/data/test directory/.hidden 2"
EOF
