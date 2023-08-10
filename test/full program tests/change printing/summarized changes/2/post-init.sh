printf "Nano Backup 123\n" > generated/files/a/file-1.txt
printf "123 Nano Backup 123\n" > generated/files/a/file-2.txt
touch -m -t 02020202 generated/files/a
printf "NANO BACKUP\n" > generated/files/a/b/c/file-3.txt
mkdir -p generated/files/a/b/c/sub/directories/
touch -m -t 02020202 generated/files/a/b/c
touch -m -t 02020202 generated/files/a/b

printf "test ABCDEFGHI\n" > generated/files/1/2/test1.txt
printf "123456787890\n" > generated/files/1/2/test-other.txt
rm generated/files/1/other.txt
rm generated/files/1/2/3/another.txt
rm generated/files/1/2/3/000.txt
find generated/files/1/ -exec touch -m -t 02020202 {} ';'

find generated/files/10/ -exec touch -m -t 02020202 {} ';'
