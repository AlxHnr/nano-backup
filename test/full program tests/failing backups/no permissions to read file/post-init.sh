echo "Hello World" > generated/no-permissions.txt
chmod 000 generated/no-permissions.txt

printf "nb: failed to open \"%s/generated/no-permissions.txt\" for reading: Permission denied\n" \
  "$PWD" >> generated/expected-output

sort generated/expected-output -o generated/expected-output
