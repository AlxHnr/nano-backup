# Used for setting the modificateion time of files independend of the
# systems time.
timestamp="03120812"
directories="copy_remove copy_remove_time content_change
content_change_time mirror_remove mirror_remove_time
track_remove track_remove_time"

mkdir generated/files/
for dir in $directories; do
  mkdir "generated/files/$dir"
done

echo "Nano-backup provides a precise way to track files. It was intended for" > generated/files/copy_remove/foo.txt
echo "backup provides a to track files was intended for" > generated/files/copy_remove_time/foo.txt

echo "TEST 123" > generated/files/content_change/foo.txt
touch -m -t "$timestamp" generated/files/content_change/foo.txt

echo "how files be backed up. apply only the last" > generated/files/mirror_remove/foo.txt
echo "Policies specify how files should be backed up. They apply only to the last" > generated/files/mirror_remove_time/foo.txt

touch generated/files/track_remove/foo.txt
touch -m -t "$timestamp" generated/files/track_remove/foo.txt
echo "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%" > generated/files/track_remove_time/foo.txt

ln -s foo.txt generated/files/copy_remove/symlink
ln -s non-existing.txt generated/files/content_change/symlink
ln -s ../content_change/foo.txt generated/files/content_change_time/symlink
ln -s /dev/null generated/files/mirror_remove/symlink

for dir in $directories; do
  touch -m -t "$timestamp" "generated/files/$dir"
done
