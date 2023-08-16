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

static StringView checkedStrCopy(StringView string, Allocator *a)
{
  StringView copy = check(strCopy(string, a));

  assert_true(copy.content != string.content);
  assert_true(copy.length == string.length);
  assert_true(copy.is_terminated);

  assert_true(memcmp(copy.content, string.content, copy.length) == 0);

  return copy;
}

static StringView checkedStrAppendPath(StringView path, StringView filename, Allocator *a)
{
  StringView string = check(strAppendPath(path, filename, a));

  assert_true(string.content != path.content);
  assert_true(string.content != filename.content);
  assert_true(string.length == path.length + filename.length + 1);
  assert_true(string.is_terminated);

  assert_true(memcmp(string.content, path.content, path.length) == 0);
  assert_true(string.content[path.length] == '/');
  assert_true(memcmp(&string.content[path.length + 1], filename.content, filename.length) == 0);
  assert_true(string.content[path.length + 1 + filename.length] == '\0');

  return string;
}

static void testStrAppendPath(const char *raw_path, const char *raw_filename, const char *expected_result,
                              Allocator *a)
{
  StringView path = checkedStr(raw_path);
  StringView filename = checkedStr(raw_filename);
  StringView result = checkedStrAppendPath(path, filename, a);
  assert_true(strIsEqual(result, checkedStr(expected_result)));
}

static void checkedStrSet(StringView *string, StringView value)
{
  strSet(string, value);
  check(*string);
  assert_true(string->content == value.content);
  assert_true(string->length == value.length);
  assert_true(string->is_terminated == value.is_terminated);
}

static StringView checkedStrStripTrailingSlashes(StringView string)
{
  StringView trimmed = check(strStripTrailingSlashes(string));
  assert_true(trimmed.content == string.content);
  assert_true(trimmed.length <= string.length);
  assert_true(trimmed.is_terminated == (trimmed.length == string.length && string.is_terminated));

  return trimmed;
}

static void testStrStripTrailingSlashes(StringView original, StringView expected)
{
  StringView trimmed = checkedStrStripTrailingSlashes(original);
  assert_true(trimmed.length == expected.length);
  assert_true(strIsEqual(trimmed, expected));
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
  assert_true(strIsEqual(split.head, expected_head));
  assert_true(strIsEqual(split.tail, expected_tail));
}

int main(void)
{
  StringView zero_length = (StringView){
    .content = "some-data",
    .length = 0,
    .is_terminated = false,
  };

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

  testGroupStart("strIsEqual()");
  {
    StringView foo = checkedStr("foo");
    StringView bar = checkedStr("bar");
    StringView empty = checkedStr("");
    StringView foo_bar = checkedStr("foo-bar");

    assert_true(strIsEqual(foo, checkedStr("foo")));
    assert_true(!strIsEqual(foo, bar));
    assert_true(!strIsEqual(foo, foo_bar));
    assert_true(strIsEqual(zero_length, checkedStr("")));
    assert_true(strIsEqual(empty, checkedStr("")));
    assert_true(strIsEqual(slice1, checkedStr("this")));
    assert_true(strIsEqual(slice2, checkedStr("is a test")));
    assert_true(strIsEqual(slice3, checkedStr("test string")));
    assert_true(!strIsEqual(slice1, checkedStr("This")));
    assert_true(!strIsEqual(slice2, checkedStr("is a Test")));
    assert_true(!strIsEqual(slice3, checkedStr("test String")));
    assert_true(!strIsEqual(slice1, slice2));
    assert_true(!strIsEqual(slice1, slice3));
    assert_true(!strIsEqual(slice2, slice3));
    assert_true(!strIsEqual(slice3, slice2));
  }
  testGroupEnd();

  testGroupStart("strCopy()");
  {
    CR_Region *r = CR_RegionNew();
    Allocator *a = allocatorWrapRegion(r);

    checkedStrCopy(checkedStr("bar"), a);

    StringView empty_copy = checkedStrCopy(checkedStr(""), a);
    assert_true(empty_copy.length == 0);

    StringView zero_length_copy = checkedStrCopy(zero_length, a);
    assert_true(zero_length_copy.length == 0);

    checkedStrCopy(slice1, a);
    checkedStrCopy(slice2, a);
    checkedStrCopy(slice3, a);

    CR_RegionRelease(r);
  }
  testGroupEnd();

  testGroupStart("strCopyRaw()");
  {
    CR_Region *r = CR_RegionNew();
    Allocator *a = allocatorWrapRegion(r);

    StringView string = str("A basic example string");
    const char *raw_string = strCopyRaw(string, a);
    assert_true(raw_string != NULL);
    assert_true(raw_string != string.content);
    assert_true(raw_string[string.length] == '\0');
    assert_true(memcmp(raw_string, string.content, string.length) == 0);

    const char *raw_empty_string = strCopyRaw(zero_length, a);
    assert_true(raw_empty_string != NULL);
    assert_true(raw_empty_string != string.content);
    assert_true(raw_empty_string[0] == '\0');

    CR_RegionRelease(r);
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

  testGroupStart("strStripTrailingSlashes()");
  {
    testStrStripTrailingSlashes(checkedStr(""), checkedStr(""));
    testStrStripTrailingSlashes(zero_length, checkedStr(""));
    testStrStripTrailingSlashes(checkedStr("foo"), checkedStr("foo"));
    testStrStripTrailingSlashes(checkedStr("/home/arch/foo-bar"), checkedStr("/home/arch/foo-bar"));
    testStrStripTrailingSlashes(checkedStr("/home/arch/foo-bar/"), checkedStr("/home/arch/foo-bar"));
    testStrStripTrailingSlashes(checkedStr("/home/arch/foo-bar//////"), checkedStr("/home/arch/foo-bar"));
    testStrStripTrailingSlashes(checkedStr("///////////////"), zero_length);
    testStrStripTrailingSlashes(checkedStr("////////////"), checkedStr(""));
    assert_true(checkedStrStripTrailingSlashes(checkedStr("/home/test")).is_terminated);
    assert_true(!checkedStrStripTrailingSlashes(checkedStr("/home/")).is_terminated);
    assert_true(checkedStrStripTrailingSlashes(checkedStr("/home")).is_terminated);
    assert_true(checkedStrStripTrailingSlashes(checkedStr("this is a test")).is_terminated);
    assert_true(checkedStrStripTrailingSlashes(checkedStr("this is a tes/t")).is_terminated);
    assert_true(!checkedStrStripTrailingSlashes(checkedStr("//////////")).is_terminated);
    assert_true(checkedStrStripTrailingSlashes(checkedStr("////////// ")).is_terminated);
  }
  testGroupEnd();

  testGroupStart("strAppendPath()");
  {
    CR_Region *r = CR_RegionNew();
    Allocator *a = allocatorWrapRegion(r);

    testStrAppendPath("", "", "/", a);
    testStrAppendPath("foo", "", "foo/", a);
    testStrAppendPath("", "bar", "/bar", a);
    testStrAppendPath("/", "", "//", a);
    testStrAppendPath("", "/", "//", a);
    testStrAppendPath("/", "/", "///", a);
    testStrAppendPath("foo", "bar", "foo/bar", a);

    testStrAppendPath("/foo/bar//", "/foo", "/foo/bar////foo", a);
    testStrAppendPath("/etc/init.d", "start.sh", "/etc/init.d/start.sh", a);
    testStrAppendPath("etc/init.d", "start.sh", "etc/init.d/start.sh", a);
    testStrAppendPath("etc/init.d", "/start.sh", "etc/init.d//start.sh", a);

    assert_true(strIsEqual(checkedStrAppendPath(slice1, slice2, a), checkedStr("this/is a test")));
    assert_true(strIsEqual(checkedStrAppendPath(slice2, slice3, a), checkedStr("is a test/test string")));
    assert_true(strIsEqual(checkedStrAppendPath(slice3, slice1, a), checkedStr("test string/this")));
    assert_true(strIsEqual(checkedStrAppendPath(slice2, zero_length, a), checkedStr("is a test/")));
    assert_true(strIsEqual(checkedStrAppendPath(zero_length, slice1, a), checkedStr("/this")));
    assert_true(strIsEqual(checkedStrAppendPath(zero_length, zero_length, a), checkedStr("/")));

    CR_RegionRelease(r);
  }
  testGroupEnd();

  testGroupStart("strSplitPath()");
  {
    PathSplit empty_split = checkedStrSplitPath(checkedStr(""));
    PathSplit empty_split2 = checkedStrSplitPath(checkedStr("/"));
    assert_true(strIsEqual(empty_split.head, empty_split2.head));
    assert_true(strIsEqual(empty_split.tail, empty_split2.tail));

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
    assert_true(strIsWhitespaceOnly(checkedStr("")));
    assert_true(strIsWhitespaceOnly(checkedStr("   ")));
    assert_true(strIsWhitespaceOnly(checkedStr("	")));
    assert_true(strIsWhitespaceOnly(checkedStr(" 	  	 ")));
    assert_true(!strIsWhitespaceOnly(checkedStr("	o ")));
    assert_true(!strIsWhitespaceOnly(checkedStr(".   ")));
    assert_true(!strIsWhitespaceOnly(checkedStr("foo")));
    assert_true(strIsWhitespaceOnly(zero_length));

    StringView string = checkedStrUnterminated("         a string.", 9);
    assert_true(strIsWhitespaceOnly(string));
  }
  testGroupEnd();

  testGroupStart("strIsEmpty()");
  {
    assert_true(strIsEmpty(checkedStr("")));
    assert_true(strIsEmpty(zero_length));
    assert_true(strIsEmpty(strUnterminated("Test 123", 0)));
    assert_true(!strIsEmpty(str("Test 123")));
    assert_true(!strIsEmpty(str(" ")));
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
