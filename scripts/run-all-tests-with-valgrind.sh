#!/bin/sh -e

# Everything should be build and linked with `-O0 -ggdb` for best results.

cd "$(dirname "$0")/.."

# Replace all binaries with a symlink to a wrapper script.
mv build/nb build/nb-bin
ln -s "$PWD/build/valgrind/run-bin.sh" build/nb
find build/test/ -type f -executable \
  -exec mv {} {}-bin \; \
  -exec ln -s "$PWD/build/valgrind/run-bin.sh" {} \;
trap 'find build/ -type l -exec rm {} \; -exec mv {}-bin {} \;' EXIT

mkdir -p build/valgrind
cat > build/valgrind/run-bin.sh <<EOF
#!/bin/sh -e

valgrind --leak-check=full --track-origins=yes --read-var-info=yes \\
  --log-file="$PWD/build/valgrind/log-%p" \\
  --quiet "\${0}-bin" "\$@"
EOF
chmod +x build/valgrind/run-bin.sh

find build/valgrind/ -type f -name 'log-*' -exec rm {} \;
make run-test

# Search for valgrind errors.
exit_code=0
find build/valgrind/ -type f -empty -exec rm {} \;
for file in build/valgrind/*; do
  test "$file" != "build/valgrind/run-bin.sh" || continue

  if test -t 1; then printf "[1;31m"; fi
  printf 'Valgrind found errors. See %s\n' "$file"
  if test -t 1; then printf "[0m"; fi

  exit_code=1
done
exit "$exit_code"
