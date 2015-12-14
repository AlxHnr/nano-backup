if [[ -f "arguments" ]]; then
  arguments=$(cat arguments)
else
  arguments="generated/repo"
fi

"$NB" $arguments 2>&1 | sort > generated/output
diff -q generated/output generated/expected-output
