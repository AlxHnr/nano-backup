# Used to simulate timestamp changes.
same_timestamp="03120812"
changed_timestamp="03120813"

# Remove mirrored files.
rm generated/files/mirror_remove/foo.txt
rm generated/files/mirror_remove/symlink
touch -m -t "$changed_timestamp" generated/files/mirror_remove

rm generated/files/mirror_remove_time/foo.txt
touch -m -t "$same_timestamp" generated/files/mirror_remove_time

# Remove tracked files.
rm generated/files/track_remove/foo.txt
touch -m -t "$changed_timestamp" generated/files/track_remove
