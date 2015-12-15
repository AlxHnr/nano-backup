mkdir -p generated

if test -f expected-output; then
  sed -r "s,^(.. )\/(.*)$,\1${PWD}/\2,g" expected-output | \
    sort > generated/expected-output
fi
