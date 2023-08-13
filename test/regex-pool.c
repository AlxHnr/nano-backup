#include "regex-pool.h"

#include "test.h"

int main(void)
{
  testGroupStart("compiling regular expressions");
  const regex_t *r1 = rpCompile(str("^foo$"), str(__FILE__), __LINE__);
  assert_true(r1 != NULL);
  assert_true(regexec(r1, "foo", 0, NULL, 0) == 0);
  assert_true(regexec(r1, "fooo", 0, NULL, 0) != 0);
  assert_true(regexec(r1, "bar", 0, NULL, 0) != 0);

  const regex_t *r2 = rpCompile(str("^(foo|bar)$"), str(__FILE__), __LINE__);
  assert_true(r2 != NULL);
  assert_true(regexec(r2, "foo", 0, NULL, 0) == 0);
  assert_true(regexec(r2, "bar", 0, NULL, 0) == 0);

  const regex_t *r3 = rpCompile(str(".*"), str(__FILE__), __LINE__);
  assert_true(r3 != NULL);

  const regex_t *r4 = rpCompile(str("^...$"), str(__FILE__), __LINE__);
  assert_true(r4 != NULL);

  const regex_t *r5 = rpCompile(str("^a"), str(__FILE__), __LINE__);
  assert_true(r5 != NULL);

  const regex_t *r6 = rpCompile(str("x"), str(__FILE__), __LINE__);
  assert_true(r6 != NULL);

  const regex_t *r7 = rpCompile(str(".?"), str(__FILE__), __LINE__);
  assert_true(r7 != NULL);

  const regex_t *r8 = rpCompile(str("a?"), str(__FILE__), __LINE__);
  assert_true(r8 != NULL);

  const regex_t *r9 = rpCompile(str("[abc]"), str(__FILE__), __LINE__);
  assert_true(r9 != NULL);

  assert_true(regexec(r1, "foo", 0, NULL, 0) == 0);
  assert_true(regexec(r1, "fooo", 0, NULL, 0) != 0);
  assert_true(regexec(r1, "bar", 0, NULL, 0) != 0);
  assert_true(regexec(r2, "foo", 0, NULL, 0) == 0);
  assert_true(regexec(r2, "bar", 0, NULL, 0) == 0);
  assert_true(regexec(r4, "bar", 0, NULL, 0) == 0);
  assert_true(regexec(r4, "baar", 0, NULL, 0) != 0);
  assert_true(regexec(r4, "xyz", 0, NULL, 0) == 0);
  assert_true(regexec(r4, "  ", 0, NULL, 0) != 0);
  assert_true(regexec(r6, "  ", 0, NULL, 0) != 0);
  assert_true(regexec(r6, " x", 0, NULL, 0) == 0);
  assert_true(regexec(r6, " \\x", 0, NULL, 0) == 0);
  assert_true(regexec(r9, "this is test", 0, NULL, 0) != 0);
  assert_true(regexec(r9, "this is a test", 0, NULL, 0) == 0);
  testGroupEnd();

  testGroupStart("error handling");
  char error_buffer[128];

  assert_error_any(rpCompile(str("^(foo|bar"), str("example.txt"), 197));
  getLastErrorMessage(error_buffer, sizeof(error_buffer));
  assert_true(strstr(error_buffer, "example.txt: line 197: ") == error_buffer);

  assert_error_any(rpCompile(str("*test*"), str("this/is/a/file.c"), 4));
  getLastErrorMessage(error_buffer, sizeof(error_buffer));
  assert_true(strstr(error_buffer, "this/is/a/file.c: line 4: ") == error_buffer);
  testGroupEnd();
}
