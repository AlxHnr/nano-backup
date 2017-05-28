# Used to simulate timestamp changes.
same_timestamp="03120812"
changed_timestamp="03120813"

# Remove copied files.
rm generated/files/copy_remove/foo.txt
rm generated/files/copy_remove/symlink
touch -m -t "$changed_timestamp" generated/files/copy_remove

rm generated/files/copy_remove_time/foo.txt
touch -m -t "$same_timestamp" generated/files/copy_remove_time

# Change tracked files.
echo "regular expression. This expression will not match recursively and can be" > generated/files/content_change/foo.txt
touch -m -t "$changed_timestamp" generated/files/content_change/foo.txt
rm generated/files/content_change/symlink
ln -s foobar1234567890abcdefg generated/files/content_change/symlink
touch -m -t "$changed_timestamp" generated/files/content_change

rm generated/files/content_change_time/symlink
ln -s /dev/foo/0987654321 generated/files/content_change_time/symlink
touch -m -t "$same_timestamp" generated/files/content_change_time

echo "??????????????????????????????????????" > generated/files/track_remove/foo.txt
touch -m -t "$same_timestamp" generated/files/track_remove/foo.txt
rm generated/files/track_remove_time/foo.txt
touch -m -t "$same_timestamp" generated/files/track_remove_time
