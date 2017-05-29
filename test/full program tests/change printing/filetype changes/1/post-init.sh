# Used for setting the modificateion time of files independend of the
# systems time.
timestamp="01010101"
directories="copy-dir copy-dir-same-time mirror-dir \
empty-directory-to-file track-dir-same-time \
track-dir-same-time/directory-to-symlink"

mkdir generated/files
for dir in $directories; do
  mkdir "generated/files/$dir"
done

echo "generated generated generated generated" > generated/files/copy-dir/file-to-symlink
ln -s /dev/null generated/files/copy-dir-same-time/symlink-to-file

echo "FOO" > generated/files/file-to-directory
ln -s ../copy-dir generated/files/mirror-dir/symlink-to-empty-directory

touch generated/files/track-dir-same-time/directory-to-symlink/a
echo "bar123789" > generated/files/track-dir-same-time/directory-to-symlink/b
echo "barbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbarbar" > \
  generated/files/track-dir-same-time/directory-to-symlink/c

for dir in $directories; do
  touch -m -t "$timestamp" "generated/files/$dir"
done
