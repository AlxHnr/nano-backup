broken_metadata="$PROJECT_PATH/test/data/dummy-metadata/invalid-path-state-type"

mkdir -p generated/repo
touch generated/repo/config
cp "$broken_metadata" generated/repo/metadata
