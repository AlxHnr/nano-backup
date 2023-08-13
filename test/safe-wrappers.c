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
  if(!test_atexit_1_called)
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
  struct dirent *dir_entry = sReadDir(dir, str(dir_path));
  assert_true(errno == 0);

  assert_true(dir_entry != NULL);
  assert_true(strcmp(dir_entry->d_name, ".") != 0);
  assert_true(strcmp(dir_entry->d_name, "..") != 0);
}

static bool checkPathExists(const char *path)
{
  assert_true(errno == 0);
  bool path_exists = sPathExists(str(path));
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
  assert_error_errno(sPathExists(str("empty.txt/foo")), "failed to check existence of \"empty.txt/foo\"", ENOTDIR);
  assert_true(checkPathExists("empty.txt"));
  assert_true(checkPathExists("example.txt"));
  assert_true(checkPathExists("symlink.txt"));
  assert_true(checkPathExists("valid-config-files"));
  assert_true(checkPathExists("./valid-config-files"));
  assert_true(checkPathExists("./valid-config-files/"));
  assert_true(checkPathExists("broken-config-files"));
  assert_true(checkPathExists("broken-config-files/"));
  assert_true(!checkPathExists("non-existing-file.txt"));
  assert_true(!checkPathExists("non-existing-directory/"));
  assert_true(!checkPathExists("non/existing/directory/"));
  assert_true(!checkPathExists("valid-config-files/non/existing/file"));

  assert_true(!sPathExists(str("tmp/dummy-symlink")));
  assert_true(symlink("non-existing-file.txt", "tmp/dummy-symlink") == 0);
  assert_true(sPathExists(str("tmp/dummy-symlink")));
  assert_true(!sPathExists(str("tmp/dummy-symlink/bar")));
  testGroupEnd();

  testGroupStart("sStat()");
  assert_error_errno(sStat(str("non-existing-file.txt")), "failed to access \"non-existing-file.txt\"", ENOENT);

  struct stat example_stat = sStat(str("symlink.txt"));
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("sLStat()");
  assert_error_errno(sLStat(str("non-existing-file.txt")), "failed to access \"non-existing-file.txt\"", ENOENT);

  example_stat = sLStat(str("symlink.txt"));
  assert_true(!S_ISREG(example_stat.st_mode));
  assert_true(S_ISLNK(example_stat.st_mode));

  example_stat = sLStat(str("example.txt"));
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("FileStream reading functions");
  assert_error_errno(sFopenRead(str("non-existing-file.txt")),
                     "failed to open \"non-existing-file.txt\" for reading", ENOENT);

  StringView example_path = str("example.txt");
  FileStream *example_read = sFopenRead(example_path);
  assert_true(example_read != NULL);

  assert_true(checkBytesLeft(example_read));
  assert_true(checkBytesLeft(example_read));

  char buffer[50] = { 0 };
  sFread(buffer, 25, example_read);

  assert_true(!checkBytesLeft(example_read));
  assert_true(!checkBytesLeft(example_read));

  assert_true(strcmp(buffer, "This is an example file.\n") == 0);

  assert_true(strEqual(fDestroy(example_read), example_path));
  assert_true(errno == 0);

  /* Try reading 50 bytes from a 25 byte long file. */
  example_read = sFopenRead(str("example.txt"));
  assert_true(example_read != NULL);
  assert_error(sFread(buffer, sizeof(buffer), example_read),
               "reading \"example.txt\": reached end of file unexpectedly");

  /* Provoke failure by reading from a write-only stream. */
  assert_error(sFread(buffer, 10, sFopenWrite(str("tmp/example-write"))),
               "IO error while reading \"tmp/example-write\"");

  /* Test sFclose(). */
  example_read = sFopenRead(str("example.txt"));
  assert_true(example_read != NULL);
  sFclose(example_read);

  /* Test sFbytesLeft(). */
  example_read = sFopenWrite(str("tmp/some-test-file.txt"));
  errno = 0;
  assert_true(!sFbytesLeft(example_read));
  assert_true(errno == 0);
  sFclose(example_read);
  assert_error_errno(sFbytesLeft(sFopenRead(str("test directory"))),
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

  assert_true(!checkBytesLeft(example_read));
  assert_true(!checkBytesLeft(example_read));
  sFclose(example_read);
  testGroupEnd();

  testGroupStart("sGetFilesContent()");
  CR_Region *r = CR_RegionNew();
  assert_error_errno(sGetFilesContent(r, str("non-existing-file.txt")),
                     "failed to "
                     "access \"non-existing-file.txt\"",
                     ENOENT);

  const FileContent example_content = sGetFilesContent(r, str("example.txt"));
  assert_true(example_content.size == 25);
  assert_true(example_content.content != NULL);
  assert_true(strncmp(example_content.content, "This is an example file.\n", 25) == 0);

  CR_RegionRelease(r);
  r = CR_RegionNew();

  const FileContent empty_content = sGetFilesContent(r, str("empty.txt"));
  assert_true(empty_content.size == 0);
  assert_true(empty_content.content != NULL);
  testGroupEnd();

  testGroupStart("FileStream writing functions");
  assert_error_errno(sFopenWrite(str("non-existing-dir/file.txt")),
                     "failed to open \"non-existing-dir/file.txt\" for writing", ENOENT);

  assert_true(!sPathExists(str("tmp/test-file-1")));
  FileStream *test_file = sFopenWrite(str("tmp/test-file-1"));
  assert_true(sPathExists(str("tmp/test-file-1")));
  assert_true(test_file != NULL);

  sFwrite("hello", 5, test_file);
  assert_true(fWrite(" ", 1, test_file));
  assert_true(fTodisk(test_file));
  assert_true(fWrite("world", 5, test_file));
  sFwrite("!", 1, test_file);
  assert_true(fTodisk(test_file));
  assert_true(fTodisk(test_file));
  sFclose(test_file);

  const FileContent test_file_1_content = sGetFilesContent(r, str("tmp/test-file-1"));
  assert_true(test_file_1_content.size == 12);
  assert_true(memcmp(test_file_1_content.content, "hello world!", 12) == 0);

  /* Assert that the path gets captured properly. */
  StringView test_file_path = str("tmp/test-file-2");

  assert_true(!sPathExists(test_file_path));
  test_file = sFopenWrite(test_file_path);
  assert_true(sPathExists(test_file_path));
  assert_true(test_file != NULL);

  assert_true(strEqual(fDestroy(test_file), test_file_path));
  assert_true(errno == 0);

  const FileContent test_file_2_content = sGetFilesContent(r, str("tmp/test-file-2"));
  assert_true(test_file_2_content.size == 0);
  assert_true(test_file_2_content.content != NULL);

  /* Test overwriting behaviour. */
  test_file = sFopenWrite(str("tmp/test-file-1"));
  assert_true(test_file != NULL);
  sFwrite("Test 1 2 3", 10, test_file);
  sFclose(test_file);

  const FileContent test_file_content = sGetFilesContent(r, str("tmp/test-file-1"));
  assert_true(test_file_content.size == 10);
  assert_true(memcmp(test_file_content.content, "Test 1 2 3", 10) == 0);
  CR_RegionRelease(r);

  /* Provoke errors by writing to a read-only stream. */
  assert_error(sFwrite("hello", 5, sFopenRead(str("example.txt"))), "failed to write to \"example.txt\"");

  test_file = sFopenRead(str("example.txt"));
  assert_true(!fWrite("hello", 5, test_file));
  sFclose(test_file);
  testGroupEnd();

  testGroupStart("sMkdir()");
  assert_true(!sPathExists(str("tmp/some-directory")));
  sMkdir(str("tmp/some-directory"));
  assert_true(sPathExists(str("tmp/some-directory")));
  assert_true(S_ISDIR(sLStat(str("tmp/some-directory")).st_mode));

  assert_error_errno(sMkdir(str("tmp/some-directory")), "failed to create directory: \"tmp/some-directory\"",
                     EEXIST);
  assert_error_errno(sMkdir(str("tmp/non-existing/foo")), "failed to create directory: \"tmp/non-existing/foo\"",
                     ENOENT);
  testGroupEnd();

  testGroupStart("sSymlink()");
  assert_true(!sPathExists(str("tmp/some-symlink")));
  sSymlink(str("foo bar 123"), str("tmp/some-symlink"));
  assert_true(sPathExists(str("tmp/some-symlink")));
  assert_true(S_ISLNK(sLStat(str("tmp/some-symlink")).st_mode));

  char some_symlink_buffer[12] = { 0 };
  assert_true(11 == readlink("tmp/some-symlink", some_symlink_buffer, sizeof(some_symlink_buffer)));
  assert_true(strcmp(some_symlink_buffer, "foo bar 123") == 0);

  assert_error_errno(sSymlink(str("test"), str("tmp/some-symlink")),
                     "failed to create symlink: \"tmp/some-symlink\"", EEXIST);
  assert_error_errno(sSymlink(str("backup"), str("tmp/non-existing/bar")),
                     "failed to create symlink: \"tmp/non-existing/bar\"", ENOENT);
  testGroupEnd();

  testGroupStart("sRename()");
  assert_true(!sPathExists(str("tmp/file-1")));
  sFclose(sFopenWrite(str("tmp/file-1")));

  assert_true(sPathExists(str("tmp/file-1")));
  assert_true(!sPathExists(str("tmp/file-2")));

  sRename(str("tmp/file-1"), str("tmp/file-2"));

  assert_true(!sPathExists(str("tmp/file-1")));
  assert_true(sPathExists(str("tmp/file-2")));

  assert_error_errno(sRename(str("non-existing-file.txt"), str("tmp/file-2")),
                     "failed to rename \"non-existing-file.txt\" to \"tmp/file-2\"", ENOENT);

  assert_true(sPathExists(str("tmp/file-2")));
  assert_true(sStat(str("tmp/file-2")).st_size == 0);

  sRename(str("tmp/file-2"), str("tmp/file-2"));
  assert_true(sStat(str("tmp/file-2")).st_size == 0);
  testGroupEnd();

  testGroupStart("sChmod()");
  sChmod(str("tmp/test-file-1"), 0600);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0600);
  sChmod(str("tmp/test-file-1"), 0404);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0404);
  sChmod(str("tmp/test-file-1"), 0544);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0544);
  sChmod(str("tmp/test-file-1"), 0644);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0644);

  sSymlink(str("test-file-1"), str("tmp/test-symlink-1"));
  sChmod(str("tmp/test-symlink-1"), 0600);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0600);
  sChmod(str("tmp/test-symlink-1"), 0404);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0404);
  sChmod(str("tmp/test-symlink-1"), 0544);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0544);
  sChmod(str("tmp/test-symlink-1"), 0644);
  assert_true((sLStat(str("tmp/test-file-1")).st_mode & ~S_IFMT) == 0644);

  assert_error_errno(sChmod(str("tmp/non-existing"), 0600), "failed to change permissions of \"tmp/non-existing\"",
                     ENOENT);
  testGroupEnd();

  testGroupStart("sChown()");
  const struct stat test_file_1_stat = sLStat(str("tmp/test-file-1"));
  sChown(str("tmp/test-file-1"), test_file_1_stat.st_uid, test_file_1_stat.st_gid);

  sSymlink(str("non-existing"), str("tmp/dangling-symlink"));
  assert_error_errno(sChown(str("tmp/dangling-symlink"), test_file_1_stat.st_uid, test_file_1_stat.st_gid),
                     "failed to change owner of \"tmp/dangling-symlink\"", ENOENT);
  testGroupEnd();

  testGroupStart("sLChown()");
  const struct stat dangling_symlink_stat = sLStat(str("tmp/dangling-symlink"));

  sLChown(str("tmp/dangling-symlink"), dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid);

  assert_error_errno(sLChown(str("tmp/non-existing"), dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid),
                     "failed to change owner of \"tmp/non-existing\"", ENOENT);
  testGroupEnd();

  testGroupStart("sUtime()");
  sUtime(str("tmp/test-file-1"), 123);
  assert_true(sLStat(str("tmp/test-file-1")).st_mtime == 123);
  sUtime(str("tmp/test-file-1"), 987654);
  assert_true(sLStat(str("tmp/test-file-1")).st_mtime == 987654);
  sUtime(str("tmp/test-file-1"), 555);
  assert_true(sLStat(str("tmp/test-file-1")).st_mtime == 555);
  sUtime(str("tmp/test-symlink-1"), 13579);
  assert_true(sLStat(str("tmp/test-file-1")).st_mtime == 13579);
  sUtime(str("tmp/test-symlink-1"), 900);
  assert_true(sLStat(str("tmp/test-file-1")).st_mtime == 900);
  sUtime(str("tmp/test-symlink-1"), 12);
  assert_true(sLStat(str("tmp/test-file-1")).st_mtime == 12);

  assert_error_errno(sUtime(str("tmp/non-existing"), 123), "failed to set timestamp of \"tmp/non-existing\"",
                     ENOENT);
  testGroupEnd();

  testGroupStart("sRemove()");
  sFclose(sFopenWrite(str("tmp/file-to-remove")));
  sMkdir(str("tmp/dir-to-remove"));
  sSymlink(str("file-to-remove"), str("tmp/link-to-remove1"));
  sSymlink(str("dir-to-remove"), str("tmp/link-to-remove2"));

  sRemove(str("tmp/link-to-remove1"));
  sRemove(str("tmp/link-to-remove2"));
  assert_true(sPathExists(str("tmp/file-to-remove")));
  assert_true(sPathExists(str("tmp/dir-to-remove")));
  assert_true(!sPathExists(str("tmp/link-to-remove1")));
  assert_true(!sPathExists(str("tmp/link-to-remove2")));

  sRemove(str("tmp/file-to-remove"));
  assert_true(!sPathExists(str("tmp/file-to-remove")));

  sRemove(str("tmp/dir-to-remove"));
  assert_true(!sPathExists(str("tmp/dir-to-remove")));

  assert_error_errno(sRemove(str("tmp/non-existing")), "failed to remove \"tmp/non-existing\"", ENOENT);
  assert_error_errno(sRemove(str("tmp/non-existing-dir/foo")), "failed to remove \"tmp/non-existing-dir/foo\"",
                     ENOENT);

  sMkdir(str("tmp/non-empty-dir"));
  sFclose(sFopenWrite(str("tmp/non-empty-dir/foo")));
  assert_error_errno(sRemove(str("tmp/non-empty-dir")), "failed to remove \"tmp/non-empty-dir\"", ENOTEMPTY);

  sRemove(str("tmp/non-empty-dir/foo"));
  sRemove(str("tmp/non-empty-dir"));
  assert_true(!sPathExists(str("tmp/non-empty-dir")));
  testGroupEnd();

  testGroupStart("sRemoveRecursively()");
  assert_true(sPathExists(str("tmp/test-file-1")));
  assert_true(sPathExists(str("tmp/test-symlink-1")));
  sRemoveRecursively(str("tmp/test-symlink-1"));
  assert_true(sPathExists(str("tmp/test-file-1")));
  assert_true(!sPathExists(str("tmp/test-symlink-1")));

  sRemoveRecursively(str("tmp/test-file-1"));
  assert_true(!sPathExists(str("tmp/test-file-1")));

  sMkdir(str("tmp/foo"));
  sFclose(sFopenWrite(str("tmp/foo/bar")));
  sSymlink(str("bar"), str("tmp/foo/123"));
  sMkdir(str("tmp/foo/1"));
  sMkdir(str("tmp/foo/1/2"));
  sMkdir(str("tmp/foo/1/2/3"));
  sMkdir(str("tmp/foo/1/2/3/4"));
  sMkdir(str("tmp/foo/1/2/3/4/5"));
  sMkdir(str("tmp/foo/1/2/3/4/6"));
  sMkdir(str("tmp/foo/1/2/3/4/7"));
  sMkdir(str("tmp/foo/1/2/3/xyz"));
  sSymlink(str("../../../.."), str("tmp/foo/1/2/3/abc"));
  sSymlink(str("../../../bar"), str("tmp/foo/1/2/bar"));
  sFclose(sFopenWrite(str("tmp/bar")));

  assert_true(sPathExists(str("tmp/foo")));
  assert_true(sPathExists(str("tmp/bar")));
  sRemoveRecursively(str("tmp/foo"));
  assert_true(!sPathExists(str("tmp/foo")));
  assert_true(sPathExists(str("tmp/bar")));

  sRemoveRecursively(str("tmp/bar"));
  assert_true(!sPathExists(str("tmp/bar")));

  assert_error_errno(sRemoveRecursively(str("")), "failed to access \"\"", ENOENT);
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
  assert_true(!sIsTTY(out_stream));
  assert_true(errno == 0);

  assert_true(fclose(out_stream) == 0);
  testGroupEnd();

  testGroupStart("sStringToSize()");
  errno = 7;

  assert_true(sStringToSize(str("0")) == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("55")) == 55);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("100982")) == 100982);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("   53")) == 53);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("+129")) == 129);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("0x17")) == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("92a7ff")) == 92);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("0777")) == 777);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("01938")) == 1938);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("28.7")) == 28);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("34,6")) == 34);
  assert_true(errno == 7);
  assert_true(sStringToSize(str("4294967295")) == 4294967295);
  assert_true(errno == 7);

#if SIZE_MAX == UINT32_MAX
  assert_error(sStringToSize(str("4294967296")), "value too large to convert to size: \"4294967296\"");
#elif SIZE_MAX == UINT64_MAX
  assert_true(sStringToSize(str("9223372036854775807")) == 9223372036854775807);
  assert_true(errno == 7);
#endif

  assert_error(sStringToSize(str("9223372036854775808")),
               "value too large to convert to size: \"9223372036854775808\"");

  assert_error(sStringToSize(str("-1")), "unable to convert negative value to size: \"-1\"");
  assert_error(sStringToSize(str("-100964")), "unable to convert negative value to size: \"-100964\"");
  assert_error(sStringToSize(str("-4294967295")), "unable to convert negative value to size: \"-4294967295\"");
  assert_error(sStringToSize(str("-4294967296")), "unable to convert negative value to size: \"-4294967296\"");
  assert_error(sStringToSize(str("-9223372036854775807")),
               "unable to convert negative value to size: \"-9223372036854775807\"");
  assert_error(sStringToSize(str("-9223372036854775808")),
               "unable to convert negative value to size: \"-9223372036854775808\"");
  assert_error(sStringToSize(str("-9223372036854775809")),
               "unable to convert negative value to size: \"-9223372036854775809\"");
  assert_error(sStringToSize(str("-99999999999999999999")),
               "unable to convert negative value to size: \"-99999999999999999999\"");

  assert_error(sStringToSize(str("")), "unable to convert to size: \"\"");
  assert_error(sStringToSize(str("foo")), "unable to convert to size: \"foo\"");
  assert_error(sStringToSize(str("  foo")), "unable to convert to size: \"  foo\"");
  assert_error(sStringToSize(str("ef68")), "unable to convert to size: \"ef68\"");
  assert_error(sStringToSize(str("--1")), "unable to convert to size: \"--1\"");
  assert_error(sStringToSize(str("++1")), "unable to convert to size: \"++1\"");
  testGroupEnd();

  testGroupStart("sTime()");
  assert_true(sTime() != (time_t)-1);
  testGroupEnd();

  testGroupStart("sOpenDir()");
  DIR *test_directory = sOpenDir(str("test directory"));
  assert_true(test_directory != NULL);

  DIR *test_foo_1 = sOpenDir(str("./test directory/foo 1/"));
  assert_true(test_foo_1 != NULL);

  assert_error_errno(sOpenDir(str("non-existing-directory")),
                     "failed to open directory \"non-existing-directory\"", ENOENT);
  testGroupEnd();

  testGroupStart("sReadDir()");
  /* Count example files in "test directory". */
  for(size_t counter = 0; counter < 17; counter++)
  {
    checkReadDir(test_directory, "test directory");
  }

  assert_true(errno == 0);
  assert_true(sReadDir(test_directory, str("test directory")) == NULL);
  assert_true(errno == 0);

  /* Count example files in "test directory/foo 1". */
  for(size_t counter = 0; counter < 5; counter++)
  {
    checkReadDir(test_foo_1, "test directory/foo 1");
  }

  assert_true(errno == 0);
  assert_true(sReadDir(test_foo_1, str("test directory/foo 1")) == NULL);
  assert_true(errno == 0);
  testGroupEnd();

  testGroupStart("sCloseDir()");
  sCloseDir(test_directory, str("test directory"));
  sCloseDir(test_foo_1, str("test directory/foo 1"));
  testGroupEnd();
}
