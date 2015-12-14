if [[ -f arguments ]]; then
  cat arguments
else
  echo generated/repo
fi | xargs "$NB" 2>&1 | sort > generated/output

diff -q generated/output generated/expected-output
