#include "safe-wrappers.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

static bool test_atexit_1_called = false;

static void testAtExit1(void)
{
  test_atexit_1_called = true;
}

static void testAtExit2(void)
{
  if(test_atexit_1_called == false)
  {
    fprintf(stderr, "fatal: behaviour of atexit() violates C99\n");
    abort();
  }
}

/** Calls sReadDir() with the given arguments and checks its result. This
  function asserts that errno doesn't get modified. Errno must be set to 0
  before this function can be called. */
static void checkReadDir(DIR *dir, const char *dir_path)
{
  assert_true(errno == 0);
  struct dirent *dir_entry = sReadDir(dir, strWrap(dir_path));
  assert_true(errno == 0);

  assert_true(dir_entry != NULL);
  assert_true(strcmp(dir_entry->d_name, ".") != 0);
  assert_true(strcmp(dir_entry->d_name, "..") != 0);
}

static bool checkPathExists(const char *path)
{
  assert_true(errno == 0);
  bool path_exists = sPathExists(strWrap(path));
  assert_true(errno == 0);

  return path_exists;
}

/** A wrapper around sFbytesLeft() which asserts that errno doesn't get
  polluted. Errno must be 0 when this function gets called.
*/
static bool checkBytesLeft(FileStream *stream)
{
  assert_true(errno == 0);
  bool bytes_left = sFbytesLeft(stream);
  assert_true(errno == 0);

  return bytes_left;
}

static void checkReadLine(FILE *stream, const char *expected_line)
{
  char *line = sReadLine(stream);
  assert_true(strcmp(line, expected_line) == 0);
  free(line);
}

/** Tests sReadLine() by reading lines from "valid-config-files/simple.txt"
  using the given file stream. */
static void checkReadSimpleTxt(FILE *stream)
{
  assert_true(stream != NULL);

  checkReadLine(stream, "[copy]");
  checkReadLine(stream, "/home/user/Pictures");
  checkReadLine(stream, "");
  checkReadLine(stream, "[mirror]");
  checkReadLine(stream, "/home/foo");
  checkReadLine(stream, "");
  checkReadLine(stream, "[track]");
  checkReadLine(stream, "/etc");
  checkReadLine(stream, "/home/user/.config");
}

int main(void)
{
  testGroupStart("sMalloc()");
  void *ptr = sMalloc(2048);
  assert_true(ptr != NULL);
  assert_error(sMalloc(0), "unable to allocate 0 bytes");
  testGroupEnd();

  testGroupStart("sRealloc()");
  ptr = sRealloc(ptr, 64);
  assert_true(ptr != NULL);

  void *ptr_backup = ptr;
  assert_error((ptr = sRealloc(ptr, 0)), "unable to reallocate 0 bytes");

  /* Assert that ptr does not change if sRealloc() fails. */
  assert_true(ptr == ptr_backup);
  free(ptr);
  testGroupEnd();

  testGroupStart("sAtexit()");
  sAtexit(testAtExit2);
  sAtexit(testAtExit1);
  testGroupEnd();

  testGroupStart("sPathExists()");
  assert_error_errno(sPathExists(strWrap("empty.txt/foo")), "failed to check existence of \"empty.txt/foo\"",
                     ENOTDIR);
  assert_true(checkPathExists("empty.txt"));
  assert_true(checkPathExists("example.txt"));
  assert_true(checkPathExists("symlink.txt"));
  assert_true(checkPathExists("valid-config-files"));
  assert_true(checkPathExists("./valid-config-files"));
  assert_true(checkPathExists("./valid-config-files/"));
  assert_true(checkPathExists("broken-config-files"));
  assert_true(checkPathExists("broken-config-files/"));
  assert_true(checkPathExists("non-existing-file.txt") == false);
  assert_true(checkPathExists("non-existing-directory/") == false);
  assert_true(checkPathExists("non/existing/directory/") == false);
  assert_true(checkPathExists("valid-config-files/non/existing/file") == false);

  assert_true(sPathExists(strWrap("tmp/dummy-symlink")) == false);
  assert_true(symlink("non-existing-file.txt", "tmp/dummy-symlink") == 0);
  assert_true(sPathExists(strWrap("tmp/dummy-symlink")));
  assert_true(sPathExists(strWrap("tmp/dummy-symlink/bar")) == false);
  testGroupEnd();

  testGroupStart("sStat()");
  assert_error_errno(sStat(strWrap("non-existing-file.txt")), "failed to access \"non-existing-file.txt\"",
                     ENOENT);

  struct stat example_stat = sStat(strWrap("symlink.txt"));
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("sLStat()");
  assert_error_errno(sLStat(strWrap("non-existing-file.txt")), "failed to access \"non-existing-file.txt\"",
                     ENOENT);

  example_stat = sLStat(strWrap("symlink.txt"));
  assert_true(!S_ISREG(example_stat.st_mode));
  assert_true(S_ISLNK(example_stat.st_mode));

  example_stat = sLStat(strWrap("example.txt"));
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("FileStream reading functions");
  assert_error_errno(sFopenRead(strWrap("non-existing-file.txt")),
                     "failed to open \"non-existing-file.txt\" for reading", ENOENT);

  String example_path = strWrap("example.txt");
  FileStream *example_read = sFopenRead(example_path);
  assert_true(example_read != NULL);

  assert_true(checkBytesLeft(example_read));
  assert_true(checkBytesLeft(example_read));

  char buffer[50] = { 0 };
  sFread(buffer, 25, example_read);

  assert_true(checkBytesLeft(example_read) == false);
  assert_true(checkBytesLeft(example_read) == false);

  assert_true(strcmp(buffer, "This is an example file.\n") == 0);

  assert_true(strEqual(Fdestroy(example_read), example_path));
  assert_true(errno == 0);

  /* Try reading 50 bytes from a 25 byte long file. */
  example_read = sFopenRead(strWrap("example.txt"));
  assert_true(example_read != NULL);
  assert_error(sFread(buffer, sizeof(buffer), example_read),
               "reading \"example.txt\": reached end of file unexpectedly");

  /* Provoke failure by reading from a write-only stream. */
  assert_error(sFread(buffer, 10, sFopenWrite(strWrap("tmp/example-write"))),
               "IO error while reading \"tmp/example-write\"");

  /* Test sFclose(). */
  example_read = sFopenRead(strWrap("example.txt"));
  assert_true(example_read != NULL);
  sFclose(example_read);

  /* Test sFbytesLeft(). */
  example_read = sFopenWrite(strWrap("tmp/some-test-file.txt"));
  errno = 0;
  assert_true(!sFbytesLeft(example_read));
  assert_true(errno == 0);
  sFclose(example_read);
  assert_error_errno(sFbytesLeft(sFopenRead(strWrap("test directory"))),
                     "failed to check for remaining bytes in \"test directory\"", EISDIR);

  example_read = sFopenRead(example_path);
  assert_true(example_read != NULL);

  assert_true(checkBytesLeft(example_read));
  assert_true(checkBytesLeft(example_read));
  memset(buffer, 0, sizeof(buffer));
  sFread(buffer, 24, example_read);
  assert_true(strcmp(buffer, "This is an example file.") == 0);

  assert_true(checkBytesLeft(example_read));
  assert_true(checkBytesLeft(example_read));
  memset(buffer, 0, sizeof(buffer));
  sFread(buffer, 1, example_read);
  assert_true(strcmp(buffer, "\n") == 0);

  assert_true(checkBytesLeft(example_read) == false);
  assert_true(checkBytesLeft(example_read) == false);
  sFclose(example_read);
  testGroupEnd();

  testGroupStart("sGetFilesContent()");
  CR_Region *r = CR_RegionNew();
  assert_error_errno(sGetFilesContent(r, strWrap("non-existing-file.txt")),
                     "failed to "
                     "access \"non-existing-file.txt\"",
                     ENOENT);

  FileContent example_content = sGetFilesContent(r, strWrap("example.txt"));
  assert_true(example_content.size == 25);
  assert_true(example_content.content != NULL);
  assert_true(strncmp(example_content.content, "This is an example file.\n", 25) == 0);

  CR_RegionRelease(r);
  r = CR_RegionNew();

  FileContent empty_content = sGetFilesContent(r, strWrap("empty.txt"));
  assert_true(empty_content.size == 0);
  assert_true(empty_content.content != NULL);
  testGroupEnd();

  testGroupStart("FileStream writing functions");
  assert_error_errno(sFopenWrite(strWrap("non-existing-dir/file.txt")),
                     "failed to open \"non-existing-dir/file.txt\" for writing", ENOENT);

  assert_true(sPathExists(strWrap("tmp/test-file-1")) == false);
  FileStream *test_file = sFopenWrite(strWrap("tmp/test-file-1"));
  assert_true(sPathExists(strWrap("tmp/test-file-1")));
  assert_true(test_file != NULL);

  sFwrite("hello", 5, test_file);
  assert_true(Fwrite(" ", 1, test_file) == true);
  assert_true(Ftodisk(test_file) == true);
  assert_true(Fwrite("world", 5, test_file) == true);
  sFwrite("!", 1, test_file);
  assert_true(Ftodisk(test_file) == true);
  assert_true(Ftodisk(test_file) == true);
  sFclose(test_file);

  FileContent test_file_1_content = sGetFilesContent(r, strWrap("tmp/test-file-1"));
  assert_true(test_file_1_content.size == 12);
  assert_true(memcmp(test_file_1_content.content, "hello world!", 12) == 0);

  /* Assert that the path gets captured properly. */
  String test_file_path = strWrap("tmp/test-file-2");

  assert_true(sPathExists(test_file_path) == false);
  test_file = sFopenWrite(test_file_path);
  assert_true(sPathExists(test_file_path));
  assert_true(test_file != NULL);

  assert_true(strEqual(Fdestroy(test_file), test_file_path));
  assert_true(errno == 0);

  FileContent test_file_2_content = sGetFilesContent(r, strWrap("tmp/test-file-2"));
  assert_true(test_file_2_content.size == 0);
  assert_true(test_file_2_content.content != NULL);

  /* Test overwriting behaviour. */
  test_file = sFopenWrite(strWrap("tmp/test-file-1"));
  assert_true(test_file != NULL);
  sFwrite("Test 1 2 3", 10, test_file);
  sFclose(test_file);

  FileContent test_file_content = sGetFilesContent(r, strWrap("tmp/test-file-1"));
  assert_true(test_file_content.size == 10);
  assert_true(memcmp(test_file_content.content, "Test 1 2 3", 10) == 0);
  CR_RegionRelease(r);

  /* Provoke errors by writing to a read-only stream. */
  assert_error(sFwrite("hello", 5, sFopenRead(strWrap("example.txt"))), "failed to write to \"example.txt\"");

  test_file = sFopenRead(strWrap("example.txt"));
  assert_true(Fwrite("hello", 5, test_file) == false);
  sFclose(test_file);
  testGroupEnd();

  testGroupStart("sMkdir()");
  assert_true(sPathExists(strWrap("tmp/some-directory")) == false);
  sMkdir(strWrap("tmp/some-directory"));
  assert_true(sPathExists(strWrap("tmp/some-directory")));
  assert_true(S_ISDIR(sLStat(strWrap("tmp/some-directory")).st_mode));

  assert_error_errno(sMkdir(strWrap("tmp/some-directory")), "failed to create directory: \"tmp/some-directory\"",
                     EEXIST);
  assert_error_errno(sMkdir(strWrap("tmp/non-existing/foo")),
                     "failed to create directory: \"tmp/non-existing/foo\"", ENOENT);
  testGroupEnd();

  testGroupStart("sSymlink()");
  assert_true(sPathExists(strWrap("tmp/some-symlink")) == false);
  sSymlink(strWrap("foo bar 123"), strWrap("tmp/some-symlink"));
  assert_true(sPathExists(strWrap("tmp/some-symlink")));
  assert_true(S_ISLNK(sLStat(strWrap("tmp/some-symlink")).st_mode));

  char some_symlink_buffer[12] = { 0 };
  assert_true(11 == readlink("tmp/some-symlink", some_symlink_buffer, sizeof(some_symlink_buffer)));
  assert_true(strcmp(some_symlink_buffer, "foo bar 123") == 0);

  assert_error_errno(sSymlink(strWrap("test"), strWrap("tmp/some-symlink")),
                     "failed to create symlink: \"tmp/some-symlink\"", EEXIST);
  assert_error_errno(sSymlink(strWrap("backup"), strWrap("tmp/non-existing/bar")),
                     "failed to create symlink: \"tmp/non-existing/bar\"", ENOENT);
  testGroupEnd();

  testGroupStart("sRename()");
  assert_true(sPathExists(strWrap("tmp/file-1")) == false);
  sFclose(sFopenWrite(strWrap("tmp/file-1")));

  assert_true(sPathExists(strWrap("tmp/file-1")));
  assert_true(sPathExists(strWrap("tmp/file-2")) == false);

  sRename(strWrap("tmp/file-1"), strWrap("tmp/file-2"));

  assert_true(sPathExists(strWrap("tmp/file-1")) == false);
  assert_true(sPathExists(strWrap("tmp/file-2")));

  assert_error_errno(sRename(strWrap("non-existing-file.txt"), strWrap("tmp/file-2")),
                     "failed to rename \"non-existing-file.txt\" to \"tmp/file-2\"", ENOENT);

  assert_true(sPathExists(strWrap("tmp/file-2")));
  assert_true(sStat(strWrap("tmp/file-2")).st_size == 0);

  sRename(strWrap("tmp/file-2"), strWrap("tmp/file-2"));
  assert_true(sStat(strWrap("tmp/file-2")).st_size == 0);
  testGroupEnd();

  testGroupStart("sChmod()");
  sChmod(strWrap("tmp/test-file-1"), 0600);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0600);
  sChmod(strWrap("tmp/test-file-1"), 0404);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0404);
  sChmod(strWrap("tmp/test-file-1"), 0544);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0544);
  sChmod(strWrap("tmp/test-file-1"), 0644);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0644);

  sSymlink(strWrap("test-file-1"), strWrap("tmp/test-symlink-1"));
  sChmod(strWrap("tmp/test-symlink-1"), 0600);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0600);
  sChmod(strWrap("tmp/test-symlink-1"), 0404);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0404);
  sChmod(strWrap("tmp/test-symlink-1"), 0544);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0544);
  sChmod(strWrap("tmp/test-symlink-1"), 0644);
  assert_true((sLStat(strWrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0644);

  assert_error_errno(sChmod(strWrap("tmp/non-existing"), 0600),
                     "failed to change permissions of \"tmp/non-existing\"", ENOENT);
  testGroupEnd();

  testGroupStart("sChown()");
  struct stat test_file_1_stat = sLStat(strWrap("tmp/test-file-1"));
  sChown(strWrap("tmp/test-file-1"), test_file_1_stat.st_uid, test_file_1_stat.st_gid);

  sSymlink(strWrap("non-existing"), strWrap("tmp/dangling-symlink"));
  assert_error_errno(sChown(strWrap("tmp/dangling-symlink"), test_file_1_stat.st_uid, test_file_1_stat.st_gid),
                     "failed to change owner of \"tmp/dangling-symlink\"", ENOENT);
  testGroupEnd();

  testGroupStart("sLChown()");
  struct stat dangling_symlink_stat = sLStat(strWrap("tmp/dangling-symlink"));

  sLChown(strWrap("tmp/dangling-symlink"), dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid);

  assert_error_errno(
    sLChown(strWrap("tmp/non-existing"), dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid),
    "failed to change owner of \"tmp/non-existing\"", ENOENT);
  testGroupEnd();

  testGroupStart("sUtime()");
  sUtime(strWrap("tmp/test-file-1"), 123);
  assert_true(sLStat(strWrap("tmp/test-file-1")).st_mtime == 123);
  sUtime(strWrap("tmp/test-file-1"), 987654);
  assert_true(sLStat(strWrap("tmp/test-file-1")).st_mtime == 987654);
  sUtime(strWrap("tmp/test-file-1"), 555);
  assert_true(sLStat(strWrap("tmp/test-file-1")).st_mtime == 555);
  sUtime(strWrap("tmp/test-symlink-1"), 13579);
  assert_true(sLStat(strWrap("tmp/test-file-1")).st_mtime == 13579);
  sUtime(strWrap("tmp/test-symlink-1"), 900);
  assert_true(sLStat(strWrap("tmp/test-file-1")).st_mtime == 900);
  sUtime(strWrap("tmp/test-symlink-1"), 12);
  assert_true(sLStat(strWrap("tmp/test-file-1")).st_mtime == 12);

  assert_error_errno(sUtime(strWrap("tmp/non-existing"), 123), "failed to set timestamp of \"tmp/non-existing\"",
                     ENOENT);
  testGroupEnd();

  testGroupStart("sRemove()");
  sFclose(sFopenWrite(strWrap("tmp/file-to-remove")));
  sMkdir(strWrap("tmp/dir-to-remove"));
  sSymlink(strWrap("file-to-remove"), strWrap("tmp/link-to-remove1"));
  sSymlink(strWrap("dir-to-remove"), strWrap("tmp/link-to-remove2"));

  sRemove(strWrap("tmp/link-to-remove1"));
  sRemove(strWrap("tmp/link-to-remove2"));
  assert_true(sPathExists(strWrap("tmp/file-to-remove")));
  assert_true(sPathExists(strWrap("tmp/dir-to-remove")));
  assert_true(sPathExists(strWrap("tmp/link-to-remove1")) == false);
  assert_true(sPathExists(strWrap("tmp/link-to-remove2")) == false);

  sRemove(strWrap("tmp/file-to-remove"));
  assert_true(sPathExists(strWrap("tmp/file-to-remove")) == false);

  sRemove(strWrap("tmp/dir-to-remove"));
  assert_true(sPathExists(strWrap("tmp/dir-to-remove")) == false);

  assert_error_errno(sRemove(strWrap("tmp/non-existing")), "failed to remove \"tmp/non-existing\"", ENOENT);
  assert_error_errno(sRemove(strWrap("tmp/non-existing-dir/foo")), "failed to remove \"tmp/non-existing-dir/foo\"",
                     ENOENT);

  sMkdir(strWrap("tmp/non-empty-dir"));
  sFclose(sFopenWrite(strWrap("tmp/non-empty-dir/foo")));
  assert_error_errno(sRemove(strWrap("tmp/non-empty-dir")), "failed to remove \"tmp/non-empty-dir\"", ENOTEMPTY);

  sRemove(strWrap("tmp/non-empty-dir/foo"));
  sRemove(strWrap("tmp/non-empty-dir"));
  assert_true(sPathExists(strWrap("tmp/non-empty-dir")) == false);
  testGroupEnd();

  testGroupStart("sRemoveRecursively()");
  assert_true(sPathExists(strWrap("tmp/test-file-1")));
  assert_true(sPathExists(strWrap("tmp/test-symlink-1")));
  sRemoveRecursively(strWrap("tmp/test-symlink-1"));
  assert_true(sPathExists(strWrap("tmp/test-file-1")));
  assert_true(sPathExists(strWrap("tmp/test-symlink-1")) == false);

  sRemoveRecursively(strWrap("tmp/test-file-1"));
  assert_true(sPathExists(strWrap("tmp/test-file-1")) == false);

  sMkdir(strWrap("tmp/foo"));
  sFclose(sFopenWrite(strWrap("tmp/foo/bar")));
  sSymlink(strWrap("bar"), strWrap("tmp/foo/123"));
  sMkdir(strWrap("tmp/foo/1"));
  sMkdir(strWrap("tmp/foo/1/2"));
  sMkdir(strWrap("tmp/foo/1/2/3"));
  sMkdir(strWrap("tmp/foo/1/2/3/4"));
  sMkdir(strWrap("tmp/foo/1/2/3/4/5"));
  sMkdir(strWrap("tmp/foo/1/2/3/4/6"));
  sMkdir(strWrap("tmp/foo/1/2/3/4/7"));
  sMkdir(strWrap("tmp/foo/1/2/3/xyz"));
  sSymlink(strWrap("../../../.."), strWrap("tmp/foo/1/2/3/abc"));
  sSymlink(strWrap("../../../bar"), strWrap("tmp/foo/1/2/bar"));
  sFclose(sFopenWrite(strWrap("tmp/bar")));

  assert_true(sPathExists(strWrap("tmp/foo")));
  assert_true(sPathExists(strWrap("tmp/bar")));
  sRemoveRecursively(strWrap("tmp/foo"));
  assert_true(sPathExists(strWrap("tmp/foo")) == false);
  assert_true(sPathExists(strWrap("tmp/bar")));

  sRemoveRecursively(strWrap("tmp/bar"));
  assert_true(sPathExists(strWrap("tmp/bar")) == false);

  assert_error_errno(sRemoveRecursively(strWrap("")), "failed to access \"\"", ENOENT);
  testGroupEnd();

  testGroupStart("sGetCwd()");
  errno = 22;
  char *cwd = sGetCwd();
  assert_true(cwd != NULL);
  assert_true(errno == 22);

  char *cwd_copy = sMalloc(strlen(cwd) + 1);
  assert_true(getcwd(cwd_copy, strlen(cwd) + 1) == cwd_copy);
  assert_true(strcmp(cwd, cwd_copy) == 0);

  free(cwd);
  free(cwd_copy);
  testGroupEnd();

  testGroupStart("sReadLine()");
  FILE *in_stream = fopen("valid-config-files/simple.txt", "rb");
  checkReadSimpleTxt(in_stream);
  assert_true(feof(in_stream) == 0);
  assert_true(sReadLine(in_stream) == NULL);
  assert_true(feof(in_stream));
  assert_true(sReadLine(in_stream) == NULL);
  assert_true(sReadLine(in_stream) == NULL);
  assert_true(fclose(in_stream) == 0);

  in_stream = fopen("valid-config-files/simple-noeol.txt", "rb");
  checkReadSimpleTxt(in_stream);
  assert_true(feof(in_stream));
  assert_true(sReadLine(in_stream) == NULL);
  assert_true(feof(in_stream));
  assert_true(sReadLine(in_stream) == NULL);
  assert_true(sReadLine(in_stream) == NULL);
  assert_true(fclose(in_stream) == 0);
  testGroupEnd();

  testGroupStart("sIsTTY()");
  FILE *out_stream = fopen("tmp/file-1", "wb");
  assert_true(out_stream != NULL);

  errno = 0;
  assert_true(sIsTTY(out_stream) == false);
  assert_true(errno == 0);

  assert_true(fclose(out_stream) == 0);
  testGroupEnd();

  testGroupStart("sStringToSize()");
  errno = 7;

  assert_true(sStringToSize(strWrap("0")) == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("55")) == 55);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("100982")) == 100982);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("   53")) == 53);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("+129")) == 129);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("0x17")) == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("92a7ff")) == 92);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("0777")) == 777);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("01938")) == 1938);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("28.7")) == 28);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("34,6")) == 34);
  assert_true(errno == 7);
  assert_true(sStringToSize(strWrap("4294967295")) == 4294967295);
  assert_true(errno == 7);

#if SIZE_MAX == UINT32_MAX
  assert_error(sStringToSize(strWrap("4294967296")), "value too large to convert to size: \"4294967296\"");
#elif SIZE_MAX == UINT64_MAX
  assert_true(sStringToSize(strWrap("9223372036854775807")) == 9223372036854775807);
  assert_true(errno == 7);
#endif

  assert_error(sStringToSize(strWrap("9223372036854775808")),
               "value too large to convert to size: \"9223372036854775808\"");

  assert_error(sStringToSize(strWrap("-1")), "unable to convert negative value to size: \"-1\"");
  assert_error(sStringToSize(strWrap("-100964")), "unable to convert negative value to size: \"-100964\"");
  assert_error(sStringToSize(strWrap("-4294967295")), "unable to convert negative value to size: \"-4294967295\"");
  assert_error(sStringToSize(strWrap("-4294967296")), "unable to convert negative value to size: \"-4294967296\"");
  assert_error(sStringToSize(strWrap("-9223372036854775807")),
               "unable to convert negative value to size: \"-9223372036854775807\"");
  assert_error(sStringToSize(strWrap("-9223372036854775808")),
               "unable to convert negative value to size: \"-9223372036854775808\"");
  assert_error(sStringToSize(strWrap("-9223372036854775809")),
               "unable to convert negative value to size: \"-9223372036854775809\"");
  assert_error(sStringToSize(strWrap("-99999999999999999999")),
               "unable to convert negative value to size: \"-99999999999999999999\"");

  assert_error(sStringToSize(strWrap("")), "unable to convert to size: \"\"");
  assert_error(sStringToSize(strWrap("foo")), "unable to convert to size: \"foo\"");
  assert_error(sStringToSize(strWrap("  foo")), "unable to convert to size: \"  foo\"");
  assert_error(sStringToSize(strWrap("ef68")), "unable to convert to size: \"ef68\"");
  assert_error(sStringToSize(strWrap("--1")), "unable to convert to size: \"--1\"");
  assert_error(sStringToSize(strWrap("++1")), "unable to convert to size: \"++1\"");
  testGroupEnd();

  testGroupStart("sTime()");
  assert_true(sTime() != (time_t)-1);
  testGroupEnd();

  testGroupStart("sOpenDir()");
  DIR *test_directory = sOpenDir(strWrap("test directory"));
  assert_true(test_directory != NULL);

  DIR *test_foo_1 = sOpenDir(strWrap("./test directory/foo 1/"));
  assert_true(test_foo_1 != NULL);

  assert_error_errno(sOpenDir(strWrap("non-existing-directory")),
                     "failed to open directory \"non-existing-directory\"", ENOENT);
  testGroupEnd();

  testGroupStart("sReadDir()");
  /* Count example files in "test directory". */
  for(size_t counter = 0; counter < 17; counter++)
  {
    checkReadDir(test_directory, "test directory");
  }

  assert_true(errno == 0);
  assert_true(sReadDir(test_directory, strWrap("test directory")) == NULL);
  assert_true(errno == 0);

  /* Count example files in "test directory/foo 1". */
  for(size_t counter = 0; counter < 5; counter++)
  {
    checkReadDir(test_foo_1, "test directory/foo 1");
  }

  assert_true(errno == 0);
  assert_true(sReadDir(test_foo_1, strWrap("test directory/foo 1")) == NULL);
  assert_true(errno == 0);
  testGroupEnd();

  testGroupStart("sCloseDir()");
  sCloseDir(test_directory, strWrap("test directory"));
  sCloseDir(test_foo_1, strWrap("test directory/foo 1"));
  testGroupEnd();
}
