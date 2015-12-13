"$NB" generated/repo |& sort > generated/output
diff -q generated/output generated/expected-output
