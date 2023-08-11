die()
{
  printf "%s\n" "$*" >&2
  exit 1
}

assertNBFails()
(
  exit_status=0
  printf 'yes\n' | "$NB" "$@" > generated/output 2>&1 || exit_status=$?

  test "$exit_status" -eq 1 ||
    die "wrong exit status: $exit_status (expected 1)"
  diff -q generated/output generated/expected-output
  test -e generated/repo/lockfile ||
    die "lockfile owned by another process was deleted: \"generated/repo/lockfile\""
)

sleep 20 & # Must be high enough to not time out with Valgrind.
timeout_pid=$!
killTimeout()( kill -s "$1" "$timeout_pid" >/dev/null 2>&1 )
trap 'killTimeout 15' EXIT

while killTimeout 0; do true; done | "$NB" generated/repo >/dev/null &
while killTimeout 0 && test ! -e generated/repo/lockfile; do true; done
test -e generated/repo/lockfile ||
  die "nb does not create lockfile during backup"

assertNBFails generated/repo

# Test that the previous run didn't replace the original lockfile with its
# own.
assertNBFails generated/repo

# Same test, but trough another branch in main().
touch generated/repo/metadata
assertNBFails generated/repo gc
