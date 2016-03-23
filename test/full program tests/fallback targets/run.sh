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
