# Used to simulate timestamp changes.
same_timestamp="01010101"
changed_timestamp="02020202"

rm generated/files/copy-dir/file-to-symlink
ln -s ../../files generated/files/copy-dir/file-to-symlink
touch -m -t "$changed_timestamp" generated/files/copy-dir

rm generated/files/copy-dir-same-time/symlink-to-file
echo "Permission is granted to anyone to use this software for any purpose" > \
  generated/files/copy-dir-same-time/symlink-to-file
touch -m -t "$same_timestamp" generated/files/copy-dir-same-time

rm generated/files/file-to-directory
mkdir generated/files/file-to-directory
touch generated/files/file-to-directory/test.txt
echo "3. This notice may not be removed or altered from any source" > \
  generated/files/file-to-directory/text.txt
