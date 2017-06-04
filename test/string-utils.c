/** @file
  Tests helper functions for string manipulation.
*/

#include "string-utils.h"

#include <string.h>

#include "test.h"

/** Applies strRemoveTrailingSlashes() to the given string and checks its
  output.

  @param original The string that should be trimmed.
  @param expected The expected result.
*/
static void testRemoveTrailingSlashes(String original, String expected)
{
  String trimmed = strRemoveTrailingSlashes(original);
  assert_true(trimmed.length == expected.length);
  assert_true(strCompare(trimmed, expected));

  /* Assert that the string doesn't get reallocated. */
  assert_true(trimmed.str == original.str);
}

/** Simplified wrapper around strIsParentPath(). */
static bool isParentPath(const char *parent, const char *path)
{
  return strIsParentPath(str(parent), str(path));
}

/** Tests strPathContainsDotElements(). */
static void testStrPathContainsDotElements(void)
{
  assert_true(strPathContainsDotElements(str("")) == false);
  assert_true(strPathContainsDotElements(str(".")));
  assert_true(strPathContainsDotElements(str("..")));
  assert_true(strPathContainsDotElements(str("...")) == false);
  assert_true(strPathContainsDotElements(str("....")) == false);
  assert_true(strPathContainsDotElements(str("/.")));
  assert_true(strPathContainsDotElements(str("/..")));
  assert_true(strPathContainsDotElements(str("/...")) == false);
  assert_true(strPathContainsDotElements(str("/....")) == false);
  assert_true(strPathContainsDotElements(str("./")));
  assert_true(strPathContainsDotElements(str("../")));
  assert_true(strPathContainsDotElements(str(".../")) == false);
  assert_true(strPathContainsDotElements(str("..../")) == false);
  assert_true(strPathContainsDotElements(str("/./")));
  assert_true(strPathContainsDotElements(str("/../")));
  assert_true(strPathContainsDotElements(str("/.../")) == false);
  assert_true(strPathContainsDotElements(str("/..../")) == false);
  assert_true(strPathContainsDotElements(str("//.")) == false);
  assert_true(strPathContainsDotElements(str("//..")) == false);
  assert_true(strPathContainsDotElements(str("//...")) == false);
  assert_true(strPathContainsDotElements(str("//....")) == false);
  assert_true(strPathContainsDotElements(str(".//")));
  assert_true(strPathContainsDotElements(str("..//")));
  assert_true(strPathContainsDotElements(str("...//")) == false);
  assert_true(strPathContainsDotElements(str("....//")) == false);
  assert_true(strPathContainsDotElements(str("//.//")) == false);
  assert_true(strPathContainsDotElements(str("//..//")) == false);
  assert_true(strPathContainsDotElements(str("//...//")) == false);
  assert_true(strPathContainsDotElements(str("//....//")) == false);
  assert_true(strPathContainsDotElements(str("///.")) == false);
  assert_true(strPathContainsDotElements(str("///..")) == false);
  assert_true(strPathContainsDotElements(str("///...")) == false);
  assert_true(strPathContainsDotElements(str("///....")) == false);
  assert_true(strPathContainsDotElements(str(".///")));
  assert_true(strPathContainsDotElements(str("..///")));
  assert_true(strPathContainsDotElements(str("...///")) == false);
  assert_true(strPathContainsDotElements(str("....///")) == false);
  assert_true(strPathContainsDotElements(str("///.///")) == false);
  assert_true(strPathContainsDotElements(str("///..///")) == false);
  assert_true(strPathContainsDotElements(str("///...///")) == false);
  assert_true(strPathContainsDotElements(str("///....///")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/hidden/bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/.hidden/bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/..hidden/bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/...hidden/bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/hidden./bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/hidden../bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/hidden.../bar")) == false);
  assert_true(strPathContainsDotElements(str("./home/foo/")));
  assert_true(strPathContainsDotElements(str("../home/foo/")));
  assert_true(strPathContainsDotElements(str(".../home/foo/")) == false);
  assert_true(strPathContainsDotElements(str("..../home/foo/")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo/.")));
  assert_true(strPathContainsDotElements(str("/home/foo/..")));
  assert_true(strPathContainsDotElements(str("home/foo/.")));
  assert_true(strPathContainsDotElements(str("home/foo/..")));
  assert_true(strPathContainsDotElements(str("/home/foo.")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo..")) == false);
  assert_true(strPathContainsDotElements(str("home/foo.")) == false);
  assert_true(strPathContainsDotElements(str("home/foo..")) == false);
  assert_true(strPathContainsDotElements(str("home/foo...")) == false);
  assert_true(strPathContainsDotElements(str("/home/.foo")) == false);
  assert_true(strPathContainsDotElements(str("/home/..foo")) == false);
  assert_true(strPathContainsDotElements(str("home/.foo")) == false);
  assert_true(strPathContainsDotElements(str("home/..foo")) == false);
  assert_true(strPathContainsDotElements(str("home/...foo")) == false);
  assert_true(strPathContainsDotElements(str("home/./foo")));
  assert_true(strPathContainsDotElements(str("home/../foo")));
  assert_true(strPathContainsDotElements(str("/home/./foo")));
  assert_true(strPathContainsDotElements(str("/home/../foo")));
  assert_true(strPathContainsDotElements(str("home/./foo/")));
  assert_true(strPathContainsDotElements(str("home/../foo/")));
  assert_true(strPathContainsDotElements(str("/home/./foo/")));
  assert_true(strPathContainsDotElements(str("/home/../foo/")));
  assert_true(strPathContainsDotElements(str("home//./foo/")) == false);
  assert_true(strPathContainsDotElements(str("/home///./foo/")) == false);
  assert_true(strPathContainsDotElements(str("/home////./foo/")) == false);
  assert_true(strPathContainsDotElements(str("/home////./foo/.")));
  assert_true(strPathContainsDotElements(str("/home/.///./foo/")));
  assert_true(strPathContainsDotElements(str("/home/..//foo/")));
  assert_true(strPathContainsDotElements(str(".home/foo/bar")) == false);
  assert_true(strPathContainsDotElements(str("..home/foo/bar")) == false);
  assert_true(strPathContainsDotElements(str("...home/foo/bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo////////bar/.")));
  assert_true(strPathContainsDotElements(str("/home/foo////////bar/..")));
  assert_true(strPathContainsDotElements(str("/home/foo////.////bar////")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo////..////bar////")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo////...////bar////")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo////////bar")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo////////bar/")) == false);
  assert_true(strPathContainsDotElements(str("/home/f/o//////bar////")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo////////bar////")) == false);
  assert_true(strPathContainsDotElements(str("/home/foo////......////bar////")) == false);
  assert_true(strPathContainsDotElements(str("///////////")) == false);
  assert_true(strPathContainsDotElements(str(".///////////")));
  assert_true(strPathContainsDotElements(str("..///////////")));
  assert_true(strPathContainsDotElements(str("...///////////")) == false);
  assert_true(strPathContainsDotElements(str(".../////./../////")));
  assert_true(strPathContainsDotElements(str(".../////x/../////")));
  assert_true(strPathContainsDotElements(str(".//////./////")));
  assert_true(strPathContainsDotElements(str(".//////../////")));
  assert_true(strPathContainsDotElements(str("../////.//////")));
  assert_true(strPathContainsDotElements(str(".//////../////..")));
  assert_true(strPathContainsDotElements(str("../////..//////.")));
}

int main(void)
{
  String zero_length = (String){ .str = "some-data", .length = 0 };

  testGroupStart("str()");
  const char *raw_foo = "foo";
  String foo = str(raw_foo);

  assert_true(foo.length == 3);
  assert_true(foo.str == raw_foo);

  assert_true(str("").length == 0);
  assert_true(str("").str != NULL);
  testGroupEnd();

  testGroupStart("strCopy()");
  String bar = str("bar");
  String bar_copy = strCopy(bar);

  assert_true(bar_copy.str != bar.str);
  assert_true(bar_copy.length == bar.length);
  assert_true(strcmp(bar_copy.str, bar.str) == 0);
  assert_true(bar_copy.str[bar_copy.length] == '\0');

  String empty = str("");
  String empty_copy = strCopy(empty);

  assert_true(empty_copy.length == 0);
  assert_true(empty_copy.str != empty.str);
  assert_true(strcmp(empty_copy.str, empty.str) == 0);
  assert_true(empty_copy.str[0] == '\0');

  String zero_length_copy = strCopy(zero_length);
  assert_true(zero_length_copy.length == 0);
  assert_true(zero_length_copy.str[0] == '\0');
  assert_true(zero_length_copy.str != zero_length.str);
  testGroupEnd();

  testGroupStart("strCompare()");
  String foo_bar = str("foo-bar");
  assert_true(strCompare(foo, str("foo")));
  assert_true(strCompare(foo, foo_bar) == false);
  assert_true(strCompare(bar, bar_copy));
  assert_true(strCompare(zero_length, str("")));
  assert_true(strCompare(empty, str("")));
  testGroupEnd();

  testGroupStart("strWhitespaceOnly()");
  assert_true(strWhitespaceOnly(str("")));
  assert_true(strWhitespaceOnly(str("   ")));
  assert_true(strWhitespaceOnly(str("	")));
  assert_true(strWhitespaceOnly(str(" 	  	 ")));
  assert_true(strWhitespaceOnly(str("	o ")) == false);
  assert_true(strWhitespaceOnly(str(".   ")) == false);
  assert_true(strWhitespaceOnly(str("foo")) == false);
  assert_true(strWhitespaceOnly(zero_length));
  testGroupEnd();

  testGroupStart("strHash()");
  assert_true(strHash(str("")) == strHash(zero_length));
  testGroupEnd();

  testGroupStart("strRemoveTrailingSlashes()");
  testRemoveTrailingSlashes(str(""),     str(""));
  testRemoveTrailingSlashes(zero_length, str(""));
  testRemoveTrailingSlashes(str("foo"),  str("foo"));
  testRemoveTrailingSlashes(str("/home/arch/foo-bar"),
                            str("/home/arch/foo-bar"));
  testRemoveTrailingSlashes(str("/home/arch/foo-bar/"),
                            str("/home/arch/foo-bar"));
  testRemoveTrailingSlashes(str("/home/arch/foo-bar//////"),
                            str("/home/arch/foo-bar"));
  testGroupEnd();

  testGroupStart("strAppendPath()");
  String empty_empty    = strAppendPath(empty, empty);
  String foo_empty      = strAppendPath(foo,   empty);
  String empty_bar      = strAppendPath(empty, bar);
  String foo_bar_append = strAppendPath(foo,   bar);
  String foo_bar_foo    = strAppendPath(str("/foo/bar//"), str("/foo"));

  assert_true(strCompare(empty_empty,    str("/")));
  assert_true(strCompare(foo_empty,      str("foo/")));
  assert_true(strCompare(empty_bar,      str("/bar")));
  assert_true(strCompare(foo_bar_append, str("foo/bar")));
  assert_true(strCompare(foo_bar_foo,    str("/foo/bar////foo")));

  assert_true(empty_empty.str[empty_empty.length]       == '\0');
  assert_true(foo_empty.str[foo_empty.length]           == '\0');
  assert_true(empty_bar.str[empty_bar.length]           == '\0');
  assert_true(foo_bar_append.str[foo_bar_append.length] == '\0');
  assert_true(foo_bar_foo.str[foo_bar_foo.length]       == '\0');
  testGroupEnd();

  testGroupStart("strSplitPath()");
  StringSplit empty_split = strSplitPath(str(""));
  StringSplit empty_split2 = strSplitPath(str("/"));
  assert_true(strCompare(empty_split.head, empty_split2.head));
  assert_true(strCompare(empty_split.tail, empty_split2.tail));

  StringSplit no_slash = strSplitPath(str("no-slash"));
  assert_true(no_slash.head.length == 0);
  assert_true(strCompare(no_slash.tail, str("no-slash")));

  StringSplit home_path = strSplitPath(str("/home"));
  assert_true(home_path.head.length == 0);
  assert_true(strCompare(home_path.tail, str("home")));

  StringSplit some_path = strSplitPath(str("some/path/"));
  assert_true(some_path.tail.length == 0);
  assert_true(strCompare(some_path.head, str("some/path")));

  StringSplit obvious_split = strSplitPath(str("obvious/split"));
  assert_true(strCompare(obvious_split.head, str("obvious")));
  assert_true(strCompare(obvious_split.tail, str("split")));

  StringSplit pending_slashes = strSplitPath(str("/////"));
  assert_true(strCompare(pending_slashes.head, str("")));
  assert_true(strCompare(pending_slashes.tail, str("////")));

  StringSplit trailing_slash = strSplitPath(str("a//"));
  assert_true(strCompare(trailing_slash.head, str("a")));
  assert_true(strCompare(trailing_slash.tail, str("/")));

  StringSplit many_slashes = strSplitPath(str("/many/////slashes"));
  assert_true(strCompare(many_slashes.head, str("/many")));
  assert_true(strCompare(many_slashes.tail, str("////slashes")));

  StringSplit another_split = strSplitPath(str("/another/////split/"));
  assert_true(strCompare(another_split.head, str("/another/////split")));
  assert_true(strCompare(another_split.tail, str("")));
  testGroupEnd();

  testGroupStart("strIsDotElement()");
  assert_true(strIsDotElement(str("")) == false);
  assert_true(strIsDotElement(str(".")));
  assert_true(strIsDotElement(str("..")));
  assert_true(strIsDotElement(str(".hidden")) == false);
  assert_true(strIsDotElement(str("...")) == false);
  assert_true(strIsDotElement(str(",,")) == false);
  assert_true(strIsDotElement(str("aa")) == false);
  assert_true(strIsDotElement(str(".......")) == false);
  assert_true(strIsDotElement(str("./")) == false);
  assert_true(strIsDotElement(str("../")) == false);
  assert_true(strIsDotElement(str(".../")) == false);
  assert_true(strIsDotElement(str("/.")) == false);
  assert_true(strIsDotElement(str("/..")) == false);
  assert_true(strIsDotElement(str("/...")) == false);
  assert_true(strIsDotElement(str("/./")) == false);
  assert_true(strIsDotElement(str("/../")) == false);
  assert_true(strIsDotElement(str("/.../")) == false);
  assert_true(strIsDotElement((String){ .str = "...", .length = 0 }) == false);
  assert_true(strIsDotElement((String){ .str = "...", .length = 1 }));
  assert_true(strIsDotElement((String){ .str = "...", .length = 2 }));
  assert_true(strIsDotElement((String){ .str = "...", .length = 3 }) == false);
  assert_true(strIsDotElement((String){ .str = ".xx", .length = 1 }));
  assert_true(strIsDotElement((String){ .str = "..x", .length = 1 }));
  assert_true(strIsDotElement((String){ .str = "..x", .length = 2 }));
  assert_true(strIsDotElement((String){ .str = "..x", .length = 3 }) == false);
  assert_true(strIsDotElement((String){ .str = ".,,", .length = 1 }));
  assert_true(strIsDotElement((String){ .str = "..,", .length = 1 }));
  assert_true(strIsDotElement((String){ .str = "..,", .length = 2 }));
  assert_true(strIsDotElement((String){ .str = "..,", .length = 3 }) == false);
  assert_true(strIsDotElement((String){ .str = ".qq", .length = 1 }));
  assert_true(strIsDotElement((String){ .str = "..q", .length = 1 }));
  assert_true(strIsDotElement((String){ .str = "..q", .length = 2 }));
  assert_true(strIsDotElement((String){ .str = "..q", .length = 3 }) == false);
  testGroupEnd();

  testGroupStart("strPathContainsDotElements()");
  testStrPathContainsDotElements();
  testGroupEnd();

  testGroupStart("strIsParentPath()");
  assert_true(isParentPath("",              "")     == false);
  assert_true(isParentPath("",              "/")    == false);
  assert_true(isParentPath("",              "///")  == false);
  assert_true(isParentPath("/",             "")     == false);
  assert_true(isParentPath("/",             "/etc") == false);
  assert_true(isParentPath("",              "/etc"));
  assert_true(isParentPath("",              "/etc/portage"));
  assert_true(isParentPath("/",             "/etc/portage") == false);
  assert_true(isParentPath("/et",           "/etc/portage") == false);
  assert_true(isParentPath("/et",           "/et//portage"));
  assert_true(isParentPath("/etc",          "/etc/portage"));
  assert_true(isParentPath("/etc",          "/etc/portage/"));
  assert_true(isParentPath("/etc",          "/etc/portage///"));
  assert_true(isParentPath("/et?",          "/etc/portage")    == false);
  assert_true(isParentPath("/etc/",         "/etc/portage")    == false);
  assert_true(isParentPath("/etc/p",        "/etc/portage")    == false);
  assert_true(isParentPath("/etc/portage",  "/etc/portage")    == false);
  assert_true(isParentPath("/etc/portage",  "/etc/portage/")   == false);
  assert_true(isParentPath("/etc/portage",  "/etc/portage//")  == false);
  assert_true(isParentPath("/etc/portage",  "/etc/portage///") == false);
  assert_true(isParentPath("/etc/portage/", "/etc/portage") == false);
  assert_true(isParentPath("/etc/portage/", "/etc/") == false);
  assert_true(isParentPath("/etc/portage/", "/etc") == false);
  assert_true(isParentPath("/etc/portage/", "") == false);
  assert_true(isParentPath("/etc/portage",  "/etc/portage/make.conf/foo"));
  assert_true(isParentPath("/etc/portage/", "/etc/portage/make.conf/foo") == false);
  assert_true(isParentPath("",                           "/etc/portage/make.conf/foo"));
  assert_true(isParentPath("/etc",                       "/etc/portage/make.conf/foo"));
  assert_true(isParentPath("/etc/portage",               "/etc/portage/make.conf/foo"));
  assert_true(isParentPath("/etc/portage/make.conf",     "/etc/portage/make.conf/foo"));
  assert_true(isParentPath("/etc/portage/make.conf/foo", "/etc/portage/make.conf/foo") == false);
  assert_true(isParentPath("foo",           "foo/a"));
  assert_true(isParentPath("foo/a",         "foo/a/bar"));
  assert_true(isParentPath("foo/a/bar",     "foo/a/bar/1"));
  assert_true(isParentPath("foo/a/bar/1",   "foo/a/bar/1/2"));
  assert_true(isParentPath("foo/a/bar/1/2", "foo/a/bar/1/2/3"));
  assert_true(isParentPath("foo/a/bar/2/2", "foo/a/bar/1/2/3") == false);
  assert_true(isParentPath("/etc",  "/etc//")  == false);
  assert_true(isParentPath("/etc/", "/etc//")  == false);
  assert_true(isParentPath("/etc/", "/etc///") == false);
  assert_true(isParentPath("/etc/", "/etc//portage"));
  assert_true(isParentPath("/etc/", "/etc///portage"));
  testGroupEnd();
}
