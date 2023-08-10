mkdir -p generated/files/a/b/c/d

mkdir -p generated/files/1/2/3
printf "Test 123\n" > generated/files/1/2/test1.txt
printf "Test 2345\n" > generated/files/1/2/test-other.txt
printf "TEST abcdefg\n" > generated/files/1/other.txt
printf "TEST TEST TEST\n" > generated/files/1/2/3/another.txt
printf "0000000000\n" > generated/files/1/2/3/000.txt

mkdir -p generated/files/10/20/30
mkdir -p generated/files/unchanged/

find generated/files/ -exec touch -m -t 01010101 {} ';'
