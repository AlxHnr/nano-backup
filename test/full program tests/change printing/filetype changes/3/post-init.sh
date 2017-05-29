# Used to simulate timestamp changes.
same_timestamp="01010101"
changed_timestamp="02020202"

rm generated/files/mirror-dir/symlink-to-empty-directory
mkdir generated/files/mirror-dir/symlink-to-empty-directory
touch -m -t "$changed_timestamp" generated/files/mirror-dir

rmdir generated/files/empty-directory-to-file
echo "FILE CONTENT 123" > generated/files/empty-directory-to-file

rm generated/files/track-dir-same-time/directory-to-symlink/a
rm generated/files/track-dir-same-time/directory-to-symlink/b
rm generated/files/track-dir-same-time/directory-to-symlink/c
rmdir generated/files/track-dir-same-time/directory-to-symlink
ln -s non-existing.txt generated/files/track-dir-same-time/directory-to-symlink
touch -m -t "$same_timestamp" generated/files/track-dir-same-time
