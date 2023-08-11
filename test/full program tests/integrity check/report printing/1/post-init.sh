mkdir generated/files/
yes 'Content 1' | head -n 250 > generated/files/file1.txt
yes 'Content 1' | head -n 250 > generated/files/file2.txt # Duplicate

mkdir generated/files/another-directory/
yes '== CONTENT 20 ==' | head -n 400 > generated/files/another-directory/sample.txt
yes 'nano backup 123' | head -n 300 > generated/files/another-directory/sample01.txt
