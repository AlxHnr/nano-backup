#include "str.h"

#include <string.h>

#include "test.h"

static String check(String string)
{
  assert_true(string.content != NULL);

  if(string.is_terminated)
  {
    assert_true(string.content[string.length] == '\0');
  }

  return string;
}

static String checkedStrWrap(const char *cstring)
{
  String string = check(strWrap(cstring));

  assert_true(string.length == strlen(cstring));
  assert_true(string.content == cstring);
  assert_true(string.is_terminated);

  return string;
}

static String checkedStrWrapLength(const char *string, const size_t length)
{
  String slice = check(strWrapLength(string, length));
  assert_true(slice.content == string);
  assert_true(slice.length == length);
  assert_true(!slice.is_terminated);

  return slice;
}

static String checkedStrCopy(String string)
{
  String copy = check(strCopy(string));

  assert_true(copy.content != string.content);
  assert_true(copy.length == string.length);
  assert_true(copy.is_terminated);

  assert_true(memcmp(copy.content, string.content, copy.length) == 0);

  return copy;
}

static String checkedStrAppendPath(String a, String b)
{
  String string = check(strAppendPath(a, b));

  assert_true(string.content != a.content);
  assert_true(string.content != b.content);
  assert_true(string.length == a.length + b.length + 1);
  assert_true(string.is_terminated);

  assert_true(memcmp(string.content, a.content, a.length) == 0);
  assert_true(string.content[a.length] == '/');
  assert_true(memcmp(&string.content[a.length + 1], b.content, b.length) == 0);
  assert_true(string.content[a.length + 1 + b.length] == '\0');

  return string;
}

/** Tests strAppendPath().

  @param ca The first string to pass to strAppendPath().
  @param cb The second string to pass to strAppendPath().
  @param cexpected_result The expected result.
*/
static void testStrAppendPath(const char *ca, const char *cb, const char *cexpected_result)
{
  String a = checkedStrWrap(ca);
  String b = checkedStrWrap(cb);
  String result = checkedStrAppendPath(a, b);
  String expected_result = checkedStrWrap(cexpected_result);

  assert_true(strEqual(result, expected_result));
}

static void checkedStrSet(String *string, String value)
{
  strSet(string, value);
  check(*string);
  assert_true(string->content == value.content);
  assert_true(string->length == value.length);
  assert_true(string->is_terminated == value.is_terminated);
}

static const char *checkedStrRaw(String string, char **buffer)
{
  if(string.is_terminated)
  {
    const char *old_buffer = *buffer;

    const char *cstring = strRaw(string, buffer);
    assert_true(cstring == string.content);

    assert_true(*buffer == old_buffer);

    return cstring;
  }
  else
  {
    const char *cstring = strRaw(string, buffer);
    assert_true(cstring != NULL);
    assert_true(cstring != string.content);
    assert_true(cstring == *buffer);
    assert_true(cstring[string.length] == '\0');
    assert_true(memcmp(cstring, string.content, string.length) == 0);

    return cstring;
  }
}

static String checkedStrRemoveTrailingSlashes(String string)
{
  String trimmed = check(strRemoveTrailingSlashes(string));
  assert_true(trimmed.content == string.content);
  assert_true(trimmed.length <= string.length);
  assert_true(trimmed.is_terminated == (trimmed.length == string.length && string.is_terminated));

  return trimmed;
}

static void testStrRemoveTrailingSlashes(String original, String expected)
{
  String trimmed = checkedStrRemoveTrailingSlashes(original);
  assert_true(trimmed.length == expected.length);
  assert_true(strEqual(trimmed, expected));
}

static bool isParentPath(const char *parent, const char *path)
{
  return strIsParentPath(checkedStrWrap(parent), checkedStrWrap(path));
}

static PathSplit checkedStrSplitPath(String path)
{
  PathSplit split = strSplitPath(path);
  check(split.head);
  check(split.tail);

  assert_true(split.head.content == path.content);
  assert_true(!split.head.is_terminated);
  assert_true(split.tail.is_terminated == path.is_terminated);

  assert_true(split.head.length + split.tail.length <= path.length);
  assert_true(path.length - (split.head.length + split.tail.length) <= 1);

  assert_true(split.tail.content + split.tail.length == path.content + path.length);

  return split;
}

static void testStrSplitPath(const char *cpath, const char *cexpected_head, const char *cexpected_tail)
{
  String path = checkedStrWrap(cpath);
  String expected_head = checkedStrWrap(cexpected_head);
  String expected_tail = checkedStrWrap(cexpected_tail);

  PathSplit split = checkedStrSplitPath(path);
  assert_true(strEqual(split.head, expected_head));
  assert_true(strEqual(split.tail, expected_tail));
}

int main(void)
{
  testGroupStart("strWrap()");
  {
    checkedStrWrap("");
    checkedStrWrap("foo");
    checkedStrWrap("bar");
    checkedStrWrap("foo bar");
  }
  testGroupEnd();

  testGroupStart("StrWrapLength()");
  const char *cstring = "this is a test string";

  String slice1 = checkedStrWrapLength(cstring, 4);
  String slice2 = checkedStrWrapLength(&cstring[5], 9);
  String slice3 = checkedStrWrapLength(&cstring[10], 11);
  testGroupEnd();

  testGroupStart("strCopy()");
  String zero_length = (String){
    .content = "some-data",
    .length = 0,
    .is_terminated = false,
  };
  {
    String bar = checkedStrWrap("bar");
    checkedStrCopy(bar);

    String empty = checkedStrWrap("");
    String empty_copy = checkedStrCopy(empty);
    assert_true(empty_copy.length == 0);

    String zero_length_copy = checkedStrCopy(zero_length);
    assert_true(zero_length_copy.length == 0);

    checkedStrCopy(slice1);
    checkedStrCopy(slice2);
    checkedStrCopy(slice3);
  }
  testGroupEnd();

  testGroupStart("strSet()");
  {
    String string = checkedStrWrap("");
    checkedStrSet(&string, checkedStrWrap("Dummy string"));
    checkedStrSet(&string, checkedStrWrap("ABC 123"));
    checkedStrSet(&string, checkedStrWrap("Nano backup"));
    checkedStrSet(&string, slice1);
    checkedStrSet(&string, slice2);
    checkedStrSet(&string, slice3);
  }
  testGroupEnd();

  testGroupStart("strEqual()");
  {
    String foo = checkedStrWrap("foo");
    String bar = checkedStrWrap("bar");
    String empty = checkedStrWrap("");
    String foo_bar = checkedStrWrap("foo-bar");

    assert_true(strEqual(foo, checkedStrWrap("foo")));
    assert_true(!strEqual(foo, bar));
    assert_true(!strEqual(foo, foo_bar));
    assert_true(strEqual(zero_length, checkedStrWrap("")));
    assert_true(strEqual(empty, checkedStrWrap("")));
    assert_true(strEqual(slice1, checkedStrWrap("this")));
    assert_true(strEqual(slice2, checkedStrWrap("is a test")));
    assert_true(strEqual(slice3, checkedStrWrap("test string")));
    assert_true(!strEqual(slice1, checkedStrWrap("This")));
    assert_true(!strEqual(slice2, checkedStrWrap("is a Test")));
    assert_true(!strEqual(slice3, checkedStrWrap("test String")));
    assert_true(!strEqual(slice1, slice2));
    assert_true(!strEqual(slice1, slice3));
    assert_true(!strEqual(slice2, slice3));
    assert_true(!strEqual(slice3, slice2));
  }
  testGroupEnd();

  testGroupStart("strRaw()");
  {
    char *buffer = NULL;
    String string = checkedStrWrap(cstring);

    checkedStrRaw(string, &buffer);
    checkedStrRaw(slice1, &buffer);
    checkedStrRaw(slice2, &buffer);
    checkedStrRaw(slice3, &buffer);
  }
  testGroupEnd();

  testGroupStart("strRemoveTrailingSlashes()");
  {
    testStrRemoveTrailingSlashes(checkedStrWrap(""), checkedStrWrap(""));
    testStrRemoveTrailingSlashes(zero_length, checkedStrWrap(""));
    testStrRemoveTrailingSlashes(checkedStrWrap("foo"), checkedStrWrap("foo"));
    testStrRemoveTrailingSlashes(checkedStrWrap("/home/arch/foo-bar"), checkedStrWrap("/home/arch/foo-bar"));
    testStrRemoveTrailingSlashes(checkedStrWrap("/home/arch/foo-bar/"), checkedStrWrap("/home/arch/foo-bar"));
    testStrRemoveTrailingSlashes(checkedStrWrap("/home/arch/foo-bar//////"), checkedStrWrap("/home/arch/foo-bar"));
    testStrRemoveTrailingSlashes(checkedStrWrap("///////////////"), zero_length);
    testStrRemoveTrailingSlashes(checkedStrWrap("////////////"), checkedStrWrap(""));
    assert_true(checkedStrRemoveTrailingSlashes(checkedStrWrap("/home/test")).is_terminated);
    assert_true(!checkedStrRemoveTrailingSlashes(checkedStrWrap("/home/")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStrWrap("/home")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStrWrap("this is a test")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStrWrap("this is a tes/t")).is_terminated);
    assert_true(!checkedStrRemoveTrailingSlashes(checkedStrWrap("//////////")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStrWrap("////////// ")).is_terminated);
  }
  testGroupEnd();

  testGroupStart("strAppendPath()");
  {
    testStrAppendPath("", "", "/");
    testStrAppendPath("foo", "", "foo/");
    testStrAppendPath("", "bar", "/bar");
    testStrAppendPath("/", "", "//");
    testStrAppendPath("", "/", "//");
    testStrAppendPath("/", "/", "///");
    testStrAppendPath("foo", "bar", "foo/bar");

    testStrAppendPath("/foo/bar//", "/foo", "/foo/bar////foo");
    testStrAppendPath("/etc/init.d", "start.sh", "/etc/init.d/start.sh");
    testStrAppendPath("etc/init.d", "start.sh", "etc/init.d/start.sh");
    testStrAppendPath("etc/init.d", "/start.sh", "etc/init.d//start.sh");

    assert_true(strEqual(checkedStrAppendPath(slice1, slice2), checkedStrWrap("this/is a test")));
    assert_true(strEqual(checkedStrAppendPath(slice2, slice3), checkedStrWrap("is a test/test string")));
    assert_true(strEqual(checkedStrAppendPath(slice3, slice1), checkedStrWrap("test string/this")));
    assert_true(strEqual(checkedStrAppendPath(slice2, zero_length), checkedStrWrap("is a test/")));
    assert_true(strEqual(checkedStrAppendPath(zero_length, slice1), checkedStrWrap("/this")));
    assert_true(strEqual(checkedStrAppendPath(zero_length, zero_length), checkedStrWrap("/")));
  }
  testGroupEnd();

  testGroupStart("strSplitPath()");
  {
    PathSplit empty_split = checkedStrSplitPath(checkedStrWrap(""));
    PathSplit empty_split2 = checkedStrSplitPath(checkedStrWrap("/"));
    assert_true(strEqual(empty_split.head, empty_split2.head));
    assert_true(strEqual(empty_split.tail, empty_split2.tail));

    String no_slash = checkedStrWrap("no-slash");
    testStrSplitPath("no-slash", "", "no-slash");
    assert_true(checkedStrSplitPath(no_slash).tail.content == no_slash.content);

    testStrSplitPath("/home", "", "home");
    testStrSplitPath("some/path/", "some/path", "");
    testStrSplitPath("some-path/", "some-path", "");
    testStrSplitPath("/some-path", "", "some-path");
    testStrSplitPath("obvious/split", "obvious", "split");
    testStrSplitPath("/////", "", "////");
    testStrSplitPath("a//", "a", "/");
    testStrSplitPath("/many/////slashes", "/many", "////slashes");
    testStrSplitPath("/another/////split/", "/another/////split", "");
    testStrSplitPath("/this/is/a/path", "/this/is/a", "path");
    testStrSplitPath("/this/is/a", "/this/is", "a");
    testStrSplitPath("/this/is", "/this", "is");
    testStrSplitPath("/this", "", "this");
    testStrSplitPath("/", "", "");

    PathSplit split1 = checkedStrSplitPath(checkedStrWrap("/this/is/a/path"));
    assert_true(split1.tail.is_terminated);

    PathSplit split2 = checkedStrSplitPath(split1.head);
    assert_true(!split2.tail.is_terminated);

    PathSplit split3 = checkedStrSplitPath(split2.head);
    assert_true(!split3.tail.is_terminated);

    PathSplit split4 = checkedStrSplitPath(split3.head);
    assert_true(!split4.tail.is_terminated);
    assert_true(split4.head.length == 0);

    PathSplit split5 = checkedStrSplitPath(split4.head);
    assert_true(!split5.tail.is_terminated);
    assert_true(split5.tail.length == 0);
    assert_true(split5.head.length == 0);
  }
  testGroupEnd();

  testGroupStart("strWhitespaceOnly()");
  {
    assert_true(strWhitespaceOnly(checkedStrWrap("")));
    assert_true(strWhitespaceOnly(checkedStrWrap("   ")));
    assert_true(strWhitespaceOnly(checkedStrWrap("	")));
    assert_true(strWhitespaceOnly(checkedStrWrap(" 	  	 ")));
    assert_true(!strWhitespaceOnly(checkedStrWrap("	o ")));
    assert_true(!strWhitespaceOnly(checkedStrWrap(".   ")));
    assert_true(!strWhitespaceOnly(checkedStrWrap("foo")));
    assert_true(strWhitespaceOnly(zero_length));

    String string = checkedStrWrapLength("         a string.", 9);
    assert_true(strWhitespaceOnly(string));
  }
  testGroupEnd();

  testGroupStart("strIsDotElement()");
  {
    assert_true(!strIsDotElement(checkedStrWrap("")));
    assert_true(strIsDotElement(checkedStrWrap(".")));
    assert_true(strIsDotElement(checkedStrWrap("..")));
    assert_true(!strIsDotElement(checkedStrWrap(".hidden")));
    assert_true(!strIsDotElement(checkedStrWrap("...")));
    assert_true(!strIsDotElement(checkedStrWrap(",,")));
    assert_true(!strIsDotElement(checkedStrWrap("aa")));
    assert_true(!strIsDotElement(checkedStrWrap(".......")));
    assert_true(!strIsDotElement(checkedStrWrap("./")));
    assert_true(!strIsDotElement(checkedStrWrap("../")));
    assert_true(!strIsDotElement(checkedStrWrap(".../")));
    assert_true(!strIsDotElement(checkedStrWrap("/.")));
    assert_true(!strIsDotElement(checkedStrWrap("/..")));
    assert_true(!strIsDotElement(checkedStrWrap("/...")));
    assert_true(!strIsDotElement(checkedStrWrap("/./")));
    assert_true(!strIsDotElement(checkedStrWrap("/../")));
    assert_true(!strIsDotElement(checkedStrWrap("/.../")));
    assert_true(!strIsDotElement((String){ .content = "...", .length = 0, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "...", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "...", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((String){ .content = "...", .length = 3, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = ".xx", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "..x", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "..x", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((String){ .content = "..x", .length = 3, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = ".,,", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "..,", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "..,", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((String){ .content = "..,", .length = 3, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = ".qq", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "..q", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((String){ .content = "..q", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((String){ .content = "..q", .length = 3, .is_terminated = false }));
  }
  testGroupEnd();

  testGroupStart("strPathContainsDotElements()");
  {
    assert_true(!strPathContainsDotElements(checkedStrWrap("")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".")));
    assert_true(strPathContainsDotElements(checkedStrWrap("..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("...")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("....")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/.")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/...")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/....")));
    assert_true(strPathContainsDotElements(checkedStrWrap("./")));
    assert_true(strPathContainsDotElements(checkedStrWrap("../")));
    assert_true(!strPathContainsDotElements(checkedStrWrap(".../")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("..../")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/./")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/../")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/.../")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/..../")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//.")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//...")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//....")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".//")));
    assert_true(strPathContainsDotElements(checkedStrWrap("..//")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("...//")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("....//")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//.//")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//..//")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//...//")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("//....//")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///.")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///...")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///....")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".///")));
    assert_true(strPathContainsDotElements(checkedStrWrap("..///")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("...///")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("....///")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///.///")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///..///")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///...///")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///....///")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo/hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo/.hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo/..hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo/...hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo/hidden./bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo/hidden../bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo/hidden.../bar")));
    assert_true(strPathContainsDotElements(checkedStrWrap("./home/foo/")));
    assert_true(strPathContainsDotElements(checkedStrWrap("../home/foo/")));
    assert_true(!strPathContainsDotElements(checkedStrWrap(".../home/foo/")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("..../home/foo/")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/foo/.")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/foo/..")));
    assert_true(strPathContainsDotElements(checkedStrWrap("home/foo/.")));
    assert_true(strPathContainsDotElements(checkedStrWrap("home/foo/..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo.")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("home/foo.")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("home/foo..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("home/foo...")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/.foo")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/..foo")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("home/.foo")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("home/..foo")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("home/...foo")));
    assert_true(strPathContainsDotElements(checkedStrWrap("home/./foo")));
    assert_true(strPathContainsDotElements(checkedStrWrap("home/../foo")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/./foo")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/../foo")));
    assert_true(strPathContainsDotElements(checkedStrWrap("home/./foo/")));
    assert_true(strPathContainsDotElements(checkedStrWrap("home/../foo/")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/./foo/")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/../foo/")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("home//./foo/")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home///./foo/")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home////./foo/")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home////./foo/.")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/.///./foo/")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/..//foo/")));
    assert_true(!strPathContainsDotElements(checkedStrWrap(".home/foo/bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("..home/foo/bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("...home/foo/bar")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/foo////////bar/.")));
    assert_true(strPathContainsDotElements(checkedStrWrap("/home/foo////////bar/..")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo////.////bar////")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo////..////bar////")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo////...////bar////")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo////////bar")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo////////bar/")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/f/o//////bar////")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo////////bar////")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("/home/foo////......////bar////")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("///////////")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".///////////")));
    assert_true(strPathContainsDotElements(checkedStrWrap("..///////////")));
    assert_true(!strPathContainsDotElements(checkedStrWrap("...///////////")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".../////./../////")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".../////x/../////")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".//////./////")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".//////../////")));
    assert_true(strPathContainsDotElements(checkedStrWrap("../////.//////")));
    assert_true(strPathContainsDotElements(checkedStrWrap(".//////../////..")));
    assert_true(strPathContainsDotElements(checkedStrWrap("../////..//////.")));
  }
  testGroupEnd();

  testGroupStart("strIsParentPath()");
  {
    assert_true(!isParentPath("", ""));
    assert_true(!isParentPath("", "/"));
    assert_true(!isParentPath("", "///"));
    assert_true(!isParentPath("/", ""));
    assert_true(!isParentPath("/", "/etc"));
    assert_true(isParentPath("", "/etc"));
    assert_true(isParentPath("", "/etc/portage"));
    assert_true(!isParentPath("/", "/etc/portage"));
    assert_true(!isParentPath("/et", "/etc/portage"));
    assert_true(isParentPath("/et", "/et//portage"));
    assert_true(isParentPath("/etc", "/etc/portage"));
    assert_true(isParentPath("/etc", "/etc/portage/"));
    assert_true(isParentPath("/etc", "/etc/portage///"));
    assert_true(!isParentPath("/et?", "/etc/portage"));
    assert_true(!isParentPath("/etc/", "/etc/portage"));
    assert_true(!isParentPath("/etc/p", "/etc/portage"));
    assert_true(!isParentPath("/etc/portage", "/etc/portage"));
    assert_true(!isParentPath("/etc/portage", "/etc/portage/"));
    assert_true(!isParentPath("/etc/portage", "/etc/portage//"));
    assert_true(!isParentPath("/etc/portage", "/etc/portage///"));
    assert_true(!isParentPath("/etc/portage/", "/etc/portage"));
    assert_true(!isParentPath("/etc/portage/", "/etc/"));
    assert_true(!isParentPath("/etc/portage/", "/etc"));
    assert_true(!isParentPath("/etc/portage/", ""));
    assert_true(isParentPath("/etc/portage", "/etc/portage/make.conf/foo"));
    assert_true(!isParentPath("/etc/portage/", "/etc/portage/make.conf/foo"));
    assert_true(isParentPath("", "/etc/portage/make.conf/foo"));
    assert_true(isParentPath("/etc", "/etc/portage/make.conf/foo"));
    assert_true(isParentPath("/etc/portage", "/etc/portage/make.conf/foo"));
    assert_true(isParentPath("/etc/portage/make.conf", "/etc/portage/make.conf/foo"));
    assert_true(!isParentPath("/etc/portage/make.conf/foo", "/etc/portage/make.conf/foo"));
    assert_true(isParentPath("foo", "foo/a"));
    assert_true(isParentPath("foo/a", "foo/a/bar"));
    assert_true(isParentPath("foo/a/bar", "foo/a/bar/1"));
    assert_true(isParentPath("foo/a/bar/1", "foo/a/bar/1/2"));
    assert_true(isParentPath("foo/a/bar/1/2", "foo/a/bar/1/2/3"));
    assert_true(!isParentPath("foo/a/bar/2/2", "foo/a/bar/1/2/3"));
    assert_true(!isParentPath("/etc", "/etc//"));
    assert_true(!isParentPath("/etc/", "/etc//"));
    assert_true(!isParentPath("/etc/", "/etc///"));
    assert_true(isParentPath("/etc/", "/etc//portage"));
    assert_true(isParentPath("/etc/", "/etc///portage"));
  }
  testGroupEnd();
}
