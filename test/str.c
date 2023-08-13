#include "str.h"

#include <string.h>

#include "CRegion/region.h"
#include "test.h"

static StringView check(StringView string)
{
  assert_true(string.content != NULL);

  if(string.is_terminated)
  {
    assert_true(string.content[string.length] == '\0');
  }

  return string;
}

static StringView checkedStr(const char *cstring)
{
  StringView string = check(str(cstring));

  assert_true(string.length == strlen(cstring));
  assert_true(string.content == cstring);
  assert_true(string.is_terminated);

  return string;
}

static StringView checkedStrUnterminated(const char *string, const size_t length)
{
  StringView slice = check(strUnterminated(string, length));
  assert_true(slice.content == string);
  assert_true(slice.length == length);
  assert_true(!slice.is_terminated);

  return slice;
}

static StringView checkedStrCopy(StringView string)
{
  StringView copy = check(strLegacyCopy(string));

  assert_true(copy.content != string.content);
  assert_true(copy.length == string.length);
  assert_true(copy.is_terminated);

  assert_true(memcmp(copy.content, string.content, copy.length) == 0);

  return copy;
}

static StringView checkedStrAppendPath(StringView a, StringView b)
{
  StringView string = check(strLegacyAppendPath(a, b));

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
  StringView a = checkedStr(ca);
  StringView b = checkedStr(cb);
  StringView result = checkedStrAppendPath(a, b);
  StringView expected_result = checkedStr(cexpected_result);

  assert_true(strEqual(result, expected_result));
}

static void checkedStrSet(StringView *string, StringView value)
{
  strSet(string, value);
  check(*string);
  assert_true(string->content == value.content);
  assert_true(string->length == value.length);
  assert_true(string->is_terminated == value.is_terminated);
}

static StringView checkedStrRemoveTrailingSlashes(StringView string)
{
  StringView trimmed = check(strRemoveTrailingSlashes(string));
  assert_true(trimmed.content == string.content);
  assert_true(trimmed.length <= string.length);
  assert_true(trimmed.is_terminated == (trimmed.length == string.length && string.is_terminated));

  return trimmed;
}

static void testStrRemoveTrailingSlashes(StringView original, StringView expected)
{
  StringView trimmed = checkedStrRemoveTrailingSlashes(original);
  assert_true(trimmed.length == expected.length);
  assert_true(strEqual(trimmed, expected));
}

static bool isParentPath(const char *parent, const char *path)
{
  return strIsParentPath(checkedStr(parent), checkedStr(path));
}

static PathSplit checkedStrSplitPath(StringView path)
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
  StringView path = checkedStr(cpath);
  StringView expected_head = checkedStr(cexpected_head);
  StringView expected_tail = checkedStr(cexpected_tail);

  PathSplit split = checkedStrSplitPath(path);
  assert_true(strEqual(split.head, expected_head));
  assert_true(strEqual(split.tail, expected_tail));
}

int main(void)
{
  testGroupStart("str()");
  {
    checkedStr("");
    checkedStr("foo");
    checkedStr("bar");
    checkedStr("foo bar");
  }
  testGroupEnd();

  testGroupStart("strUnterminated()");
  const char *cstring = "this is a test string";

  StringView slice1 = checkedStrUnterminated(cstring, 4);
  StringView slice2 = checkedStrUnterminated(&cstring[5], 9);
  StringView slice3 = checkedStrUnterminated(&cstring[10], 11);
  testGroupEnd();

  testGroupStart("strCopy()");
  StringView zero_length = (StringView){
    .content = "some-data",
    .length = 0,
    .is_terminated = false,
  };
  {
    StringView bar = checkedStr("bar");
    checkedStrCopy(bar);

    StringView empty = checkedStr("");
    StringView empty_copy = checkedStrCopy(empty);
    assert_true(empty_copy.length == 0);

    StringView zero_length_copy = checkedStrCopy(zero_length);
    assert_true(zero_length_copy.length == 0);

    checkedStrCopy(slice1);
    checkedStrCopy(slice2);
    checkedStrCopy(slice3);
  }
  testGroupEnd();

  testGroupStart("strSet()");
  {
    StringView string = checkedStr("");
    checkedStrSet(&string, checkedStr("Dummy string"));
    checkedStrSet(&string, checkedStr("ABC 123"));
    checkedStrSet(&string, checkedStr("Nano backup"));
    checkedStrSet(&string, slice1);
    checkedStrSet(&string, slice2);
    checkedStrSet(&string, slice3);
  }
  testGroupEnd();

  testGroupStart("strEqual()");
  {
    StringView foo = checkedStr("foo");
    StringView bar = checkedStr("bar");
    StringView empty = checkedStr("");
    StringView foo_bar = checkedStr("foo-bar");

    assert_true(strEqual(foo, checkedStr("foo")));
    assert_true(!strEqual(foo, bar));
    assert_true(!strEqual(foo, foo_bar));
    assert_true(strEqual(zero_length, checkedStr("")));
    assert_true(strEqual(empty, checkedStr("")));
    assert_true(strEqual(slice1, checkedStr("this")));
    assert_true(strEqual(slice2, checkedStr("is a test")));
    assert_true(strEqual(slice3, checkedStr("test string")));
    assert_true(!strEqual(slice1, checkedStr("This")));
    assert_true(!strEqual(slice2, checkedStr("is a Test")));
    assert_true(!strEqual(slice3, checkedStr("test String")));
    assert_true(!strEqual(slice1, slice2));
    assert_true(!strEqual(slice1, slice3));
    assert_true(!strEqual(slice2, slice3));
    assert_true(!strEqual(slice3, slice2));
  }
  testGroupEnd();

  testGroupStart("strGetContent(): don't allocate if not needed");
  {
    StringView string = checkedStr("A terminated C string");
    const char *raw_string = strGetContent(string, allocatorWrapAlwaysFailing());
    assert_true(raw_string == string.content);
  }
  testGroupEnd();

  testGroupStart("strGetContent(): allocate if required");
  {
    CR_Region *r = CR_RegionNew();
    StringView string = checkedStrUnterminated("This string will be cut off", 11);

    const char *raw_string = strGetContent(string, allocatorWrapRegion(r));
    assert_true(raw_string != NULL);
    assert_true(raw_string != string.content);
    assert_true(raw_string[string.length] == '\0');
    assert_true(memcmp(raw_string, string.content, string.length) == 0);

    CR_RegionRelease(r);
  }
  testGroupEnd();

  testGroupStart("strRemoveTrailingSlashes()");
  {
    testStrRemoveTrailingSlashes(checkedStr(""), checkedStr(""));
    testStrRemoveTrailingSlashes(zero_length, checkedStr(""));
    testStrRemoveTrailingSlashes(checkedStr("foo"), checkedStr("foo"));
    testStrRemoveTrailingSlashes(checkedStr("/home/arch/foo-bar"), checkedStr("/home/arch/foo-bar"));
    testStrRemoveTrailingSlashes(checkedStr("/home/arch/foo-bar/"), checkedStr("/home/arch/foo-bar"));
    testStrRemoveTrailingSlashes(checkedStr("/home/arch/foo-bar//////"), checkedStr("/home/arch/foo-bar"));
    testStrRemoveTrailingSlashes(checkedStr("///////////////"), zero_length);
    testStrRemoveTrailingSlashes(checkedStr("////////////"), checkedStr(""));
    assert_true(checkedStrRemoveTrailingSlashes(checkedStr("/home/test")).is_terminated);
    assert_true(!checkedStrRemoveTrailingSlashes(checkedStr("/home/")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStr("/home")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStr("this is a test")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStr("this is a tes/t")).is_terminated);
    assert_true(!checkedStrRemoveTrailingSlashes(checkedStr("//////////")).is_terminated);
    assert_true(checkedStrRemoveTrailingSlashes(checkedStr("////////// ")).is_terminated);
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

    assert_true(strEqual(checkedStrAppendPath(slice1, slice2), checkedStr("this/is a test")));
    assert_true(strEqual(checkedStrAppendPath(slice2, slice3), checkedStr("is a test/test string")));
    assert_true(strEqual(checkedStrAppendPath(slice3, slice1), checkedStr("test string/this")));
    assert_true(strEqual(checkedStrAppendPath(slice2, zero_length), checkedStr("is a test/")));
    assert_true(strEqual(checkedStrAppendPath(zero_length, slice1), checkedStr("/this")));
    assert_true(strEqual(checkedStrAppendPath(zero_length, zero_length), checkedStr("/")));
  }
  testGroupEnd();

  testGroupStart("strSplitPath()");
  {
    PathSplit empty_split = checkedStrSplitPath(checkedStr(""));
    PathSplit empty_split2 = checkedStrSplitPath(checkedStr("/"));
    assert_true(strEqual(empty_split.head, empty_split2.head));
    assert_true(strEqual(empty_split.tail, empty_split2.tail));

    StringView no_slash = checkedStr("no-slash");
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

    PathSplit split1 = checkedStrSplitPath(checkedStr("/this/is/a/path"));
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
    assert_true(strWhitespaceOnly(checkedStr("")));
    assert_true(strWhitespaceOnly(checkedStr("   ")));
    assert_true(strWhitespaceOnly(checkedStr("	")));
    assert_true(strWhitespaceOnly(checkedStr(" 	  	 ")));
    assert_true(!strWhitespaceOnly(checkedStr("	o ")));
    assert_true(!strWhitespaceOnly(checkedStr(".   ")));
    assert_true(!strWhitespaceOnly(checkedStr("foo")));
    assert_true(strWhitespaceOnly(zero_length));

    StringView string = checkedStrUnterminated("         a string.", 9);
    assert_true(strWhitespaceOnly(string));
  }
  testGroupEnd();

  testGroupStart("strIsDotElement()");
  {
    assert_true(!strIsDotElement(checkedStr("")));
    assert_true(strIsDotElement(checkedStr(".")));
    assert_true(strIsDotElement(checkedStr("..")));
    assert_true(!strIsDotElement(checkedStr(".hidden")));
    assert_true(!strIsDotElement(checkedStr("...")));
    assert_true(!strIsDotElement(checkedStr(",,")));
    assert_true(!strIsDotElement(checkedStr("aa")));
    assert_true(!strIsDotElement(checkedStr(".......")));
    assert_true(!strIsDotElement(checkedStr("./")));
    assert_true(!strIsDotElement(checkedStr("../")));
    assert_true(!strIsDotElement(checkedStr(".../")));
    assert_true(!strIsDotElement(checkedStr("/.")));
    assert_true(!strIsDotElement(checkedStr("/..")));
    assert_true(!strIsDotElement(checkedStr("/...")));
    assert_true(!strIsDotElement(checkedStr("/./")));
    assert_true(!strIsDotElement(checkedStr("/../")));
    assert_true(!strIsDotElement(checkedStr("/.../")));
    assert_true(!strIsDotElement((StringView){ .content = "...", .length = 0, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "...", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "...", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((StringView){ .content = "...", .length = 3, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = ".xx", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "..x", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "..x", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((StringView){ .content = "..x", .length = 3, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = ".,,", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "..,", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "..,", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((StringView){ .content = "..,", .length = 3, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = ".qq", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "..q", .length = 1, .is_terminated = false }));
    assert_true(strIsDotElement((StringView){ .content = "..q", .length = 2, .is_terminated = false }));
    assert_true(!strIsDotElement((StringView){ .content = "..q", .length = 3, .is_terminated = false }));
  }
  testGroupEnd();

  testGroupStart("strPathContainsDotElements()");
  {
    assert_true(!strPathContainsDotElements(checkedStr("")));
    assert_true(strPathContainsDotElements(checkedStr(".")));
    assert_true(strPathContainsDotElements(checkedStr("..")));
    assert_true(!strPathContainsDotElements(checkedStr("...")));
    assert_true(!strPathContainsDotElements(checkedStr("....")));
    assert_true(strPathContainsDotElements(checkedStr("/.")));
    assert_true(strPathContainsDotElements(checkedStr("/..")));
    assert_true(!strPathContainsDotElements(checkedStr("/...")));
    assert_true(!strPathContainsDotElements(checkedStr("/....")));
    assert_true(strPathContainsDotElements(checkedStr("./")));
    assert_true(strPathContainsDotElements(checkedStr("../")));
    assert_true(!strPathContainsDotElements(checkedStr(".../")));
    assert_true(!strPathContainsDotElements(checkedStr("..../")));
    assert_true(strPathContainsDotElements(checkedStr("/./")));
    assert_true(strPathContainsDotElements(checkedStr("/../")));
    assert_true(!strPathContainsDotElements(checkedStr("/.../")));
    assert_true(!strPathContainsDotElements(checkedStr("/..../")));
    assert_true(!strPathContainsDotElements(checkedStr("//.")));
    assert_true(!strPathContainsDotElements(checkedStr("//..")));
    assert_true(!strPathContainsDotElements(checkedStr("//...")));
    assert_true(!strPathContainsDotElements(checkedStr("//....")));
    assert_true(strPathContainsDotElements(checkedStr(".//")));
    assert_true(strPathContainsDotElements(checkedStr("..//")));
    assert_true(!strPathContainsDotElements(checkedStr("...//")));
    assert_true(!strPathContainsDotElements(checkedStr("....//")));
    assert_true(!strPathContainsDotElements(checkedStr("//.//")));
    assert_true(!strPathContainsDotElements(checkedStr("//..//")));
    assert_true(!strPathContainsDotElements(checkedStr("//...//")));
    assert_true(!strPathContainsDotElements(checkedStr("//....//")));
    assert_true(!strPathContainsDotElements(checkedStr("///.")));
    assert_true(!strPathContainsDotElements(checkedStr("///..")));
    assert_true(!strPathContainsDotElements(checkedStr("///...")));
    assert_true(!strPathContainsDotElements(checkedStr("///....")));
    assert_true(strPathContainsDotElements(checkedStr(".///")));
    assert_true(strPathContainsDotElements(checkedStr("..///")));
    assert_true(!strPathContainsDotElements(checkedStr("...///")));
    assert_true(!strPathContainsDotElements(checkedStr("....///")));
    assert_true(!strPathContainsDotElements(checkedStr("///.///")));
    assert_true(!strPathContainsDotElements(checkedStr("///..///")));
    assert_true(!strPathContainsDotElements(checkedStr("///...///")));
    assert_true(!strPathContainsDotElements(checkedStr("///....///")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo/hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo/.hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo/..hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo/...hidden/bar")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo/hidden./bar")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo/hidden../bar")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo/hidden.../bar")));
    assert_true(strPathContainsDotElements(checkedStr("./home/foo/")));
    assert_true(strPathContainsDotElements(checkedStr("../home/foo/")));
    assert_true(!strPathContainsDotElements(checkedStr(".../home/foo/")));
    assert_true(!strPathContainsDotElements(checkedStr("..../home/foo/")));
    assert_true(strPathContainsDotElements(checkedStr("/home/foo/.")));
    assert_true(strPathContainsDotElements(checkedStr("/home/foo/..")));
    assert_true(strPathContainsDotElements(checkedStr("home/foo/.")));
    assert_true(strPathContainsDotElements(checkedStr("home/foo/..")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo.")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo..")));
    assert_true(!strPathContainsDotElements(checkedStr("home/foo.")));
    assert_true(!strPathContainsDotElements(checkedStr("home/foo..")));
    assert_true(!strPathContainsDotElements(checkedStr("home/foo...")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/.foo")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/..foo")));
    assert_true(!strPathContainsDotElements(checkedStr("home/.foo")));
    assert_true(!strPathContainsDotElements(checkedStr("home/..foo")));
    assert_true(!strPathContainsDotElements(checkedStr("home/...foo")));
    assert_true(strPathContainsDotElements(checkedStr("home/./foo")));
    assert_true(strPathContainsDotElements(checkedStr("home/../foo")));
    assert_true(strPathContainsDotElements(checkedStr("/home/./foo")));
    assert_true(strPathContainsDotElements(checkedStr("/home/../foo")));
    assert_true(strPathContainsDotElements(checkedStr("home/./foo/")));
    assert_true(strPathContainsDotElements(checkedStr("home/../foo/")));
    assert_true(strPathContainsDotElements(checkedStr("/home/./foo/")));
    assert_true(strPathContainsDotElements(checkedStr("/home/../foo/")));
    assert_true(!strPathContainsDotElements(checkedStr("home//./foo/")));
    assert_true(!strPathContainsDotElements(checkedStr("/home///./foo/")));
    assert_true(!strPathContainsDotElements(checkedStr("/home////./foo/")));
    assert_true(strPathContainsDotElements(checkedStr("/home////./foo/.")));
    assert_true(strPathContainsDotElements(checkedStr("/home/.///./foo/")));
    assert_true(strPathContainsDotElements(checkedStr("/home/..//foo/")));
    assert_true(!strPathContainsDotElements(checkedStr(".home/foo/bar")));
    assert_true(!strPathContainsDotElements(checkedStr("..home/foo/bar")));
    assert_true(!strPathContainsDotElements(checkedStr("...home/foo/bar")));
    assert_true(strPathContainsDotElements(checkedStr("/home/foo////////bar/.")));
    assert_true(strPathContainsDotElements(checkedStr("/home/foo////////bar/..")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo////.////bar////")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo////..////bar////")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo////...////bar////")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo////////bar")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo////////bar/")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/f/o//////bar////")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo////////bar////")));
    assert_true(!strPathContainsDotElements(checkedStr("/home/foo////......////bar////")));
    assert_true(!strPathContainsDotElements(checkedStr("///////////")));
    assert_true(strPathContainsDotElements(checkedStr(".///////////")));
    assert_true(strPathContainsDotElements(checkedStr("..///////////")));
    assert_true(!strPathContainsDotElements(checkedStr("...///////////")));
    assert_true(strPathContainsDotElements(checkedStr(".../////./../////")));
    assert_true(strPathContainsDotElements(checkedStr(".../////x/../////")));
    assert_true(strPathContainsDotElements(checkedStr(".//////./////")));
    assert_true(strPathContainsDotElements(checkedStr(".//////../////")));
    assert_true(strPathContainsDotElements(checkedStr("../////.//////")));
    assert_true(strPathContainsDotElements(checkedStr(".//////../////..")));
    assert_true(strPathContainsDotElements(checkedStr("../////..//////.")));
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
