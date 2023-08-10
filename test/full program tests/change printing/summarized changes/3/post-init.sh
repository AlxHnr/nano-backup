touch -m -t 03030303 generated/files/a/b/
touch -m -t 03030303 generated/files/a/b/c

printf '\n' > generated/files/1/2/test1.txt
chmod +x generated/files/1/2/test1.txt
touch -m -t 03030303 generated/files/1/2/test1.txt
touch -m -t 03030303 generated/files/1/2

touch -m -t 03030303 generated/files/10/
