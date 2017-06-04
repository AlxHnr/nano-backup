/** @file
  Tests the regex pool.
*/

#include "regex-pool.h"

#include "test.h"

int main(void)
{
  testGroupStart("compiling regular expressions");
  const regex_t *r1 = rpCompile("^foo$", __FILE__, __LINE__);
  assert_true(r1 != NULL);
  assert_true(regexec(r1, "foo",  0, NULL, 0) == 0);
  assert_true(regexec(r1, "fooo", 0, NULL, 0) != 0);
  assert_true(regexec(r1, "bar",  0, NULL, 0) != 0);

  const regex_t *r2 = rpCompile("^(foo|bar)$", __FILE__, __LINE__);
  assert_true(r2 != NULL);
  assert_true(regexec(r2, "foo",  0, NULL, 0) == 0);
  assert_true(regexec(r2, "bar",  0, NULL, 0) == 0);

  const regex_t *r3 = rpCompile(".*", __FILE__, __LINE__);
  assert_true(r3 != NULL);

  const regex_t *r4 = rpCompile("^...$", __FILE__, __LINE__);
  assert_true(r4 != NULL);

  const regex_t *r5 = rpCompile("^a", __FILE__, __LINE__);
  assert_true(r5 != NULL);

  const regex_t *r6 = rpCompile("x", __FILE__, __LINE__);
  assert_true(r6 != NULL);

  const regex_t *r7 = rpCompile(".?", __FILE__, __LINE__);
  assert_true(r7 != NULL);

  const regex_t *r8 = rpCompile("a?", __FILE__, __LINE__);
  assert_true(r8 != NULL);

  const regex_t *r9 = rpCompile("[abc]", __FILE__, __LINE__);
  assert_true(r9 != NULL);

  assert_true(regexec(r1, "foo",  0, NULL, 0) == 0);
  assert_true(regexec(r1, "fooo", 0, NULL, 0) != 0);
  assert_true(regexec(r1, "bar",  0, NULL, 0) != 0);
  assert_true(regexec(r2, "foo",  0, NULL, 0) == 0);
  assert_true(regexec(r2, "bar",  0, NULL, 0) == 0);
  assert_true(regexec(r4, "bar",  0, NULL, 0) == 0);
  assert_true(regexec(r4, "baar", 0, NULL, 0) != 0);
  assert_true(regexec(r4, "xyz",  0, NULL, 0) == 0);
  assert_true(regexec(r4, "  ",   0, NULL, 0) != 0);
  assert_true(regexec(r6, "  ",   0, NULL, 0) != 0);
  assert_true(regexec(r6, " x",   0, NULL, 0) == 0);
  assert_true(regexec(r6, " \\x", 0, NULL, 0) == 0);
  assert_true(regexec(r9, "this is test",   0, NULL, 0) != 0);
  assert_true(regexec(r9, "this is a test", 0, NULL, 0) == 0);
  testGroupEnd();

  testGroupStart("error handling");
  assert_error(rpCompile("^(foo|bar", "example.txt", 197),
               "example.txt: line 197: Unmatched ( or \\(: \"^(foo|bar\"");
  assert_error(rpCompile("*test*", "this/is/a/file.c", 4),
               "this/is/a/file.c: line 4: Invalid preceding regular expression: \"*test*\"");
  testGroupEnd();
}
