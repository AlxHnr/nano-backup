if test -f arguments; then
  cat arguments
else
  echo generated/repo
fi |
xargs sh -c '(test -f input && cat input) | "$NB" "$@"' -- 2>&1 |
sort > generated/output

diff -q generated/output generated/expected-output

if test -f expected-repo-files; then
  ls -A generated/repo/ | sort > generated/repo-files
  diff -q generated/repo-files generated/expected-repo-files
fi

# Check generated filenames inside the repository.
for file in generated/repo/*-*-*; do
  test -e "$file" || continue

  hash=${file#*-}
  hash=${hash%-*}
  real_hash=$(sha1sum "$file")
  real_hash=${real_hash%% *}
  test "$hash" = "$real_hash" ||
    (echo "invalid hash in filename: \"$file\"" && false)

  size=${file##*-}
  real_size=$(stat --format="%s" "$file")
  test "$size" = "$real_size" ||
    (echo "invalid size in filename: \"$file\"" && false)
done
