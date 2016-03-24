mkdir -p generated/files/a/b/c/d/e/f/g
yes "nano-backup" | head -c 185000 > generated/files/foo.txt
yes "nano-backup" | head -c 185000 > generated/files/bar.txt
yes "nano-backup" | head -c 185000 > generated/files/a/b/c/file
yes "nano-backup" | head -c 185000 > generated/files/a/b/dummy
yes "nano"        | head -c 185000 > generated/files/a/b/text
