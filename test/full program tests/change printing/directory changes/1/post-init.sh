# Used for setting the modificateion time of files independend of the
# systems time.
timestamp="04090711"
directories="copied_remove copied_remove/foo
mirrored_remove mirrored_remove/foo"

mkdir generated/files/
for dir in $directories; do
  mkdir "generated/files/$dir"
done

echo "foo bar 123" > generated/files/copied_remove/foo/test1
echo "FileStream *reader = sFopenRead(/etc/init.conf);" > generated/files/copied_remove/foo/test2

echo "manually to ensure that tests run in the correct order." > generated/files/mirrored_remove/foo/bar1.txt
echo "to ensure tests run in the correct order." > generated/files/mirrored_remove/foo/bar2.txt
echo "  testGroupStart(some asserts);" > generated/files/mirrored_remove/foo/bar3.txt

for dir in $directories; do
  touch -m -t "$timestamp" "generated/files/$dir"
done
