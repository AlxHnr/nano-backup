changed_timestamp="04090609"

touch -m -t "$changed_timestamp" generated/files/copied_remove/foo
touch -m -t "$changed_timestamp" generated/files/mirrored_remove
touch -m -t "$changed_timestamp" generated/files/mirrored_remove/foo
touch -m -t "$changed_timestamp" generated/files/mirrored_remove/foo/bar2.txt
