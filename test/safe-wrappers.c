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

/** Wrap string for testing unterminated string view handling. */
static StringView wrap(const char *string)
{
  return strUnterminated(string, strlen(string));
}

/** Calls sDirGetNext() with the given arguments and checks its result. This
  function asserts that errno doesn't get modified. Errno must be set to 0
  before this function can be called. */
static void checkReadDir(DirIterator *dir)
{
  assert_true(errno == 0);
  StringView path = sDirGetNext(dir);
  assert_true(errno == 0);

  assert_true(path.length > 0);
  assert_true(!strEqual(path, str(".")));
  assert_true(!strEqual(path, str("..")));
}

static bool checkPathExists(const char *path)
{
  assert_true(errno == 0);
  bool path_exists = sPathExists(wrap(path));
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

static bool alwaysReturnFalse(StringView path, const struct stat *stats, void *user_data)
{
  (void)path;
  (void)stats;
  (void)user_data;

  return false;
}
static bool checkStats(StringView path, const struct stat *stats, void *user_data)
{
  (void)stats;
  (void)user_data;

  if(strEqual(path, str("tmp/file")))
  {
    assert_true(S_ISREG(stats->st_mode));
  }
  if(strEqual(path, str("tmp/test1")))
  {
    assert_true(S_ISDIR(stats->st_mode));
  }
  return false;
}
static bool dontPassNeededDirectories(StringView path, const struct stat *stats, void *user_data)
{
  (void)path;
  (void)stats;
  (void)user_data;

  assert_true(!strEqual(path, str("tmp/test1")));
  assert_true(!strEqual(path, str("tmp/test1/test2")));
  return false;
}
static bool countCalls(StringView path, const struct stat *stats, void *user_data)
{
  (void)path;
  (void)stats;
  size_t *value = user_data;
  *value += 1;
  return false;
}
static bool deleteSpecificFiles(StringView path, const struct stat *stats, void *user_data)
{
  (void)stats;
  (void)user_data;

  return strEqual(path, str("tmp/test1/test3")) || strEqual(path, str("tmp/test1/foo")) ||
    strEqual(path, str("tmp/test1/test2/file"));
}
static bool checkIfTest1DirWasProvided(StringView path, const struct stat *stats, void *user_data)
{
  (void)path;
  (void)stats;

  if(strEqual(path, str("tmp/test1")))
  {
    bool *value = user_data;
    assert_true(*value == false);
    *value = true;
  }

  return strEqual(path, str("tmp/test1/test2"));
}

static void testRemoveRecursivelyIf(void)
{
  testGroupStart("sRemoveRecursivelyIf(): dry run mode");
  sMkdir(str("tmp/test1"));
  sMkdir(str("tmp/test1/test2"));
  sMkdir(str("tmp/test1/test3"));
  sSymlink(wrap("/dev/null"), wrap("tmp/test1/foo"));
  sFclose(sFopenWrite(wrap("tmp/file")));
  sFclose(sFopenWrite(wrap("tmp/test1/test2/file")));

  sRemoveRecursivelyIf(str("tmp"), alwaysReturnFalse, NULL);

  assert_true(checkPathExists("tmp/file"));
  assert_true(checkPathExists("tmp/test1"));
  assert_true(checkPathExists("tmp/test1/foo"));
  assert_true(checkPathExists("tmp/test1/test2"));
  assert_true(checkPathExists("tmp/test1/test2/file"));
  assert_true(checkPathExists("tmp/test1/test3"));
  testGroupEnd();

  testGroupStart("sRemoveRecursivelyIf(): pass valid stats to callback");
  sRemoveRecursivelyIf(str("tmp"), checkStats, NULL);
  testGroupEnd();

  testGroupStart("sRemoveRecursivelyIf(): skip still needed directories");
  sRemoveRecursivelyIf(str("tmp"), dontPassNeededDirectories, NULL);
  testGroupEnd();

  testGroupStart("sRemoveRecursivelyIf(): pass user data to callback");
  size_t value = 0;
  sRemoveRecursivelyIf(str("tmp"), countCalls, &value);
  assert_true(value == 4);
  testGroupEnd();

  testGroupStart("sRemoveRecursivelyIf(): selective deletion");
  sRemoveRecursivelyIf(str("tmp"), deleteSpecificFiles, NULL);
  assert_true(checkPathExists("tmp/file"));
  assert_true(checkPathExists("tmp/test1"));
  assert_true(!checkPathExists("tmp/test1/foo"));
  assert_true(checkPathExists("tmp/test1/test2"));
  assert_true(!checkPathExists("tmp/test1/test2/file"));
  assert_true(!checkPathExists("tmp/test1/test3"));
  testGroupEnd();

  testGroupStart("sRemoveRecursivelyIf(): pass unneeded dirs to callback");
  bool test1_dir_was_provided = false;
  sRemoveRecursivelyIf(str("tmp"), checkIfTest1DirWasProvided, &test1_dir_was_provided);
  assert_true(test1_dir_was_provided);

  assert_true(checkPathExists("tmp/file"));
  assert_true(checkPathExists("tmp/test1"));
  assert_true(!checkPathExists("tmp/test1/foo"));
  assert_true(!checkPathExists("tmp/test1/test2"));
  assert_true(!checkPathExists("tmp/test1/test2/file"));
  assert_true(!checkPathExists("tmp/test1/test3"));
  testGroupEnd();
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
  assert_error_errno(sPathExists(wrap("empty.txt/foo")), "failed to check existence of \"empty.txt/foo\"",
                     ENOTDIR);
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

  assert_true(!sPathExists(wrap("tmp/dummy-symlink")));
  assert_true(symlink("non-existing-file.txt", "tmp/dummy-symlink") == 0);
  assert_true(sPathExists(wrap("tmp/dummy-symlink")));
  assert_true(!sPathExists(wrap("tmp/dummy-symlink/bar")));
  testGroupEnd();

  testGroupStart("sStat()");
  assert_error_errno(sStat(wrap("non-existing-file.txt")), "failed to access \"non-existing-file.txt\"", ENOENT);

  struct stat example_stat = sStat(wrap("symlink.txt"));
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("sLStat()");
  assert_error_errno(sLStat(wrap("non-existing-file.txt")), "failed to access \"non-existing-file.txt\"", ENOENT);

  example_stat = sLStat(wrap("symlink.txt"));
  assert_true(!S_ISREG(example_stat.st_mode));
  assert_true(S_ISLNK(example_stat.st_mode));

  example_stat = sLStat(wrap("example.txt"));
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("FileStream reading functions");
  assert_error_errno(sFopenRead(wrap("non-existing-file.txt")),
                     "failed to open \"non-existing-file.txt\" for reading", ENOENT);

  StringView example_path = wrap("example.txt");
  FileStream *example_read = sFopenRead(example_path);
  assert_true(example_read != NULL);

  assert_true(checkBytesLeft(example_read));
  assert_true(checkBytesLeft(example_read));

  char buffer[50] = { 0 };
  sFread(buffer, 25, example_read);

  assert_true(!checkBytesLeft(example_read));
  assert_true(!checkBytesLeft(example_read));

  assert_true(strcmp(buffer, "This is an example file.\n") == 0);

  assert_true(errno == 0);
  fDestroy(example_read);
  assert_true(errno == 0);

  /* Try reading 50 bytes from a 25 byte long file. */
  example_read = sFopenRead(wrap("example.txt"));
  assert_true(example_read != NULL);
  assert_error(sFread(buffer, sizeof(buffer), example_read),
               "reading \"example.txt\": reached end of file unexpectedly");

  /* Provoke failure by reading from a write-only stream. */
  assert_error(sFread(buffer, 10, sFopenWrite(wrap("tmp/example-write"))),
               "IO error while reading \"tmp/example-write\"");

  /* Test sFclose(). */
  example_read = sFopenRead(wrap("example.txt"));
  assert_true(example_read != NULL);
  sFclose(example_read);

  /* Test sFbytesLeft(). */
  example_read = sFopenWrite(wrap("tmp/some-test-file.txt"));
  errno = 0;
  assert_true(!sFbytesLeft(example_read));
  assert_true(errno == 0);
  sFclose(example_read);
  assert_error_errno(sFbytesLeft(sFopenRead(wrap("test directory"))),
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
  assert_error_errno(sGetFilesContent(r, wrap("non-existing-file.txt")),
                     "failed to "
                     "access \"non-existing-file.txt\"",
                     ENOENT);

  const FileContent example_content = sGetFilesContent(r, wrap("example.txt"));
  assert_true(example_content.size == 25);
  assert_true(example_content.content != NULL);
  assert_true(strncmp(example_content.content, "This is an example file.\n", 25) == 0);

  CR_RegionRelease(r);
  r = CR_RegionNew();

  const FileContent empty_content = sGetFilesContent(r, wrap("empty.txt"));
  assert_true(empty_content.size == 0);
  assert_true(empty_content.content != NULL);
  testGroupEnd();

  testGroupStart("FileStream writing functions");
  assert_error_errno(sFopenWrite(wrap("non-existing-dir/file.txt")),
                     "failed to open \"non-existing-dir/file.txt\" for writing", ENOENT);

  assert_true(!sPathExists(wrap("tmp/test-file-1")));
  FileStream *test_file = sFopenWrite(wrap("tmp/test-file-1"));
  assert_true(sPathExists(wrap("tmp/test-file-1")));
  assert_true(test_file != NULL);

  sFwrite("hello", 5, test_file);
  assert_true(fWrite(" ", 1, test_file));
  assert_true(fTodisk(test_file));
  assert_true(fWrite("world", 5, test_file));
  sFwrite("!", 1, test_file);
  assert_true(fTodisk(test_file));
  assert_true(fTodisk(test_file));
  sFclose(test_file);

  const FileContent test_file_1_content = sGetFilesContent(r, wrap("tmp/test-file-1"));
  assert_true(test_file_1_content.size == 12);
  assert_true(memcmp(test_file_1_content.content, "hello world!", 12) == 0);

  /* Assert that the path gets captured properly. */
  StringView test_file_path = wrap("tmp/test-file-2");

  assert_true(!sPathExists(test_file_path));
  test_file = sFopenWrite(test_file_path);
  assert_true(sPathExists(test_file_path));
  assert_true(test_file != NULL);

  assert_true(errno == 0);
  fDestroy(test_file);
  assert_true(errno == 0);

  const FileContent test_file_2_content = sGetFilesContent(r, wrap("tmp/test-file-2"));
  assert_true(test_file_2_content.size == 0);
  assert_true(test_file_2_content.content != NULL);

  /* Test overwriting behaviour. */
  test_file = sFopenWrite(wrap("tmp/test-file-1"));
  assert_true(test_file != NULL);
  sFwrite("Test 1 2 3", 10, test_file);
  sFclose(test_file);

  const FileContent test_file_content = sGetFilesContent(r, wrap("tmp/test-file-1"));
  assert_true(test_file_content.size == 10);
  assert_true(memcmp(test_file_content.content, "Test 1 2 3", 10) == 0);
  CR_RegionRelease(r);

  /* Provoke errors by writing to a read-only stream. */
  assert_error(sFwrite("hello", 5, sFopenRead(wrap("example.txt"))), "failed to write to \"example.txt\"");

  test_file = sFopenRead(wrap("example.txt"));
  assert_true(!fWrite("hello", 5, test_file));
  sFclose(test_file);
  testGroupEnd();

  testGroupStart("sMkdir()");
  assert_true(!sPathExists(wrap("tmp/some-directory")));
  sMkdir(wrap("tmp/some-directory"));
  assert_true(sPathExists(wrap("tmp/some-directory")));
  assert_true(S_ISDIR(sLStat(wrap("tmp/some-directory")).st_mode));

  assert_error_errno(sMkdir(wrap("tmp/some-directory")), "failed to create directory: \"tmp/some-directory\"",
                     EEXIST);
  assert_error_errno(sMkdir(wrap("tmp/non-existing/foo")), "failed to create directory: \"tmp/non-existing/foo\"",
                     ENOENT);
  testGroupEnd();

  testGroupStart("sSymlink()");
  assert_true(!sPathExists(wrap("tmp/some-symlink")));
  sSymlink(wrap("foo bar 123"), wrap("tmp/some-symlink"));
  assert_true(sPathExists(wrap("tmp/some-symlink")));
  assert_true(S_ISLNK(sLStat(wrap("tmp/some-symlink")).st_mode));

  char some_symlink_buffer[12] = { 0 };
  assert_true(11 == readlink("tmp/some-symlink", some_symlink_buffer, sizeof(some_symlink_buffer)));
  assert_true(strcmp(some_symlink_buffer, "foo bar 123") == 0);

  assert_error_errno(sSymlink(wrap("test"), wrap("tmp/some-symlink")),
                     "failed to create symlink: \"tmp/some-symlink\"", EEXIST);
  assert_error_errno(sSymlink(wrap("backup"), wrap("tmp/non-existing/bar")),
                     "failed to create symlink: \"tmp/non-existing/bar\"", ENOENT);
  testGroupEnd();

  testGroupStart("sRename()");
  assert_true(!sPathExists(wrap("tmp/file-1")));
  sFclose(sFopenWrite(wrap("tmp/file-1")));

  assert_true(sPathExists(wrap("tmp/file-1")));
  assert_true(!sPathExists(wrap("tmp/file-2")));

  sRename(wrap("tmp/file-1"), wrap("tmp/file-2"));

  assert_true(!sPathExists(wrap("tmp/file-1")));
  assert_true(sPathExists(wrap("tmp/file-2")));

  assert_error_errno(sRename(wrap("non-existing-file.txt"), wrap("tmp/file-2")),
                     "failed to rename \"non-existing-file.txt\" to \"tmp/file-2\"", ENOENT);

  assert_true(sPathExists(wrap("tmp/file-2")));
  assert_true(sStat(wrap("tmp/file-2")).st_size == 0);

  sRename(wrap("tmp/file-2"), wrap("tmp/file-2"));
  assert_true(sStat(wrap("tmp/file-2")).st_size == 0);
  testGroupEnd();

  testGroupStart("sChmod()");
  sChmod(wrap("tmp/test-file-1"), 0600);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0600);
  sChmod(wrap("tmp/test-file-1"), 0404);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0404);
  sChmod(wrap("tmp/test-file-1"), 0544);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0544);
  sChmod(wrap("tmp/test-file-1"), 0644);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0644);

  sSymlink(wrap("test-file-1"), wrap("tmp/test-symlink-1"));
  sChmod(wrap("tmp/test-symlink-1"), 0600);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0600);
  sChmod(wrap("tmp/test-symlink-1"), 0404);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0404);
  sChmod(wrap("tmp/test-symlink-1"), 0544);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0544);
  sChmod(wrap("tmp/test-symlink-1"), 0644);
  assert_true((sLStat(wrap("tmp/test-file-1")).st_mode & ~S_IFMT) == 0644);

  assert_error_errno(sChmod(wrap("tmp/non-existing"), 0600),
                     "failed to change permissions of \"tmp/non-existing\"", ENOENT);
  testGroupEnd();

  testGroupStart("sChown()");
  const struct stat test_file_1_stat = sLStat(wrap("tmp/test-file-1"));
  sChown(wrap("tmp/test-file-1"), test_file_1_stat.st_uid, test_file_1_stat.st_gid);

  sSymlink(wrap("non-existing"), wrap("tmp/dangling-symlink"));
  assert_error_errno(sChown(wrap("tmp/dangling-symlink"), test_file_1_stat.st_uid, test_file_1_stat.st_gid),
                     "failed to change owner of \"tmp/dangling-symlink\"", ENOENT);
  testGroupEnd();

  testGroupStart("sLChown()");
  const struct stat dangling_symlink_stat = sLStat(wrap("tmp/dangling-symlink"));

  sLChown(wrap("tmp/dangling-symlink"), dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid);

  assert_error_errno(sLChown(wrap("tmp/non-existing"), dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid),
                     "failed to change owner of \"tmp/non-existing\"", ENOENT);
  testGroupEnd();

  testGroupStart("sUtime()");
  sUtime(wrap("tmp/test-file-1"), 123);
  assert_true(sLStat(wrap("tmp/test-file-1")).st_mtime == 123);
  sUtime(wrap("tmp/test-file-1"), 987654);
  assert_true(sLStat(wrap("tmp/test-file-1")).st_mtime == 987654);
  sUtime(wrap("tmp/test-file-1"), 555);
  assert_true(sLStat(wrap("tmp/test-file-1")).st_mtime == 555);
  sUtime(wrap("tmp/test-symlink-1"), 13579);
  assert_true(sLStat(wrap("tmp/test-file-1")).st_mtime == 13579);
  sUtime(wrap("tmp/test-symlink-1"), 900);
  assert_true(sLStat(wrap("tmp/test-file-1")).st_mtime == 900);
  sUtime(wrap("tmp/test-symlink-1"), 12);
  assert_true(sLStat(wrap("tmp/test-file-1")).st_mtime == 12);

  assert_error_errno(sUtime(wrap("tmp/non-existing"), 123), "failed to set timestamp of \"tmp/non-existing\"",
                     ENOENT);
  testGroupEnd();

  testGroupStart("sRemove()");
  sFclose(sFopenWrite(wrap("tmp/file-to-remove")));
  sMkdir(wrap("tmp/dir-to-remove"));
  sSymlink(wrap("file-to-remove"), wrap("tmp/link-to-remove1"));
  sSymlink(wrap("dir-to-remove"), wrap("tmp/link-to-remove2"));

  sRemove(wrap("tmp/link-to-remove1"));
  sRemove(wrap("tmp/link-to-remove2"));
  assert_true(sPathExists(wrap("tmp/file-to-remove")));
  assert_true(sPathExists(wrap("tmp/dir-to-remove")));
  assert_true(!sPathExists(wrap("tmp/link-to-remove1")));
  assert_true(!sPathExists(wrap("tmp/link-to-remove2")));

  sRemove(wrap("tmp/file-to-remove"));
  assert_true(!sPathExists(wrap("tmp/file-to-remove")));

  sRemove(wrap("tmp/dir-to-remove"));
  assert_true(!sPathExists(wrap("tmp/dir-to-remove")));

  assert_error_errno(sRemove(wrap("tmp/non-existing")), "failed to remove \"tmp/non-existing\"", ENOENT);
  assert_error_errno(sRemove(wrap("tmp/non-existing-dir/foo")), "failed to remove \"tmp/non-existing-dir/foo\"",
                     ENOENT);

  sMkdir(wrap("tmp/non-empty-dir"));
  sFclose(sFopenWrite(wrap("tmp/non-empty-dir/foo")));
  assert_error_errno(sRemove(wrap("tmp/non-empty-dir")), "failed to remove \"tmp/non-empty-dir\"", ENOTEMPTY);

  sRemove(wrap("tmp/non-empty-dir/foo"));
  sRemove(wrap("tmp/non-empty-dir"));
  assert_true(!sPathExists(wrap("tmp/non-empty-dir")));
  testGroupEnd();

  testGroupStart("sRemoveRecursively()");
  assert_true(sPathExists(wrap("tmp/test-file-1")));
  assert_true(sPathExists(wrap("tmp/test-symlink-1")));
  sRemoveRecursively(wrap("tmp/test-symlink-1"));
  assert_true(sPathExists(wrap("tmp/test-file-1")));
  assert_true(!sPathExists(wrap("tmp/test-symlink-1")));

  sRemoveRecursively(wrap("tmp/test-file-1"));
  assert_true(!sPathExists(wrap("tmp/test-file-1")));

  sMkdir(wrap("tmp/foo"));
  sFclose(sFopenWrite(wrap("tmp/foo/bar")));
  sSymlink(wrap("bar"), wrap("tmp/foo/123"));
  sMkdir(wrap("tmp/foo/1"));
  sMkdir(wrap("tmp/foo/1/2"));
  sMkdir(wrap("tmp/foo/1/2/3"));
  sMkdir(wrap("tmp/foo/1/2/3/4"));
  sMkdir(wrap("tmp/foo/1/2/3/4/5"));
  sMkdir(wrap("tmp/foo/1/2/3/4/6"));
  sMkdir(wrap("tmp/foo/1/2/3/4/7"));
  sMkdir(wrap("tmp/foo/1/2/3/xyz"));
  sSymlink(wrap("../../../.."), wrap("tmp/foo/1/2/3/abc"));
  sSymlink(wrap("../../../bar"), wrap("tmp/foo/1/2/bar"));
  sFclose(sFopenWrite(wrap("tmp/bar")));

  assert_true(sPathExists(wrap("tmp/foo")));
  assert_true(sPathExists(wrap("tmp/bar")));
  sRemoveRecursively(wrap("tmp/foo"));
  assert_true(!sPathExists(wrap("tmp/foo")));
  assert_true(sPathExists(wrap("tmp/bar")));

  sRemoveRecursively(wrap("tmp/bar"));
  assert_true(!sPathExists(wrap("tmp/bar")));

  assert_error_errno(sRemoveRecursively(wrap("")), "failed to access \"\"", ENOENT);

  sRemoveRecursively(str("tmp"));
  sMkdir(str("tmp")); /* Asserts that the previous line worked. */
  testGroupEnd();

  testRemoveRecursivelyIf();

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

  assert_true(sStringToSize(wrap("0")) == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("55")) == 55);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("100982")) == 100982);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("   53")) == 53);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("+129")) == 129);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("0x17")) == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("92a7ff")) == 92);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("0777")) == 777);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("01938")) == 1938);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("28.7")) == 28);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("34,6")) == 34);
  assert_true(errno == 7);
  assert_true(sStringToSize(wrap("4294967295")) == 4294967295);
  assert_true(errno == 7);

#if SIZE_MAX == UINT32_MAX
  assert_error(sStringToSize(wrap("4294967296")), "value too large to convert to size: \"4294967296\"");
#elif SIZE_MAX == UINT64_MAX
  assert_true(sStringToSize(wrap("9223372036854775807")) == 9223372036854775807);
  assert_true(errno == 7);
#endif

  assert_error(sStringToSize(wrap("9223372036854775808")),
               "value too large to convert to size: \"9223372036854775808\"");

  assert_error(sStringToSize(wrap("-1")), "unable to convert negative value to size: \"-1\"");
  assert_error(sStringToSize(wrap("-100964")), "unable to convert negative value to size: \"-100964\"");
  assert_error(sStringToSize(wrap("-4294967295")), "unable to convert negative value to size: \"-4294967295\"");
  assert_error(sStringToSize(wrap("-4294967296")), "unable to convert negative value to size: \"-4294967296\"");
  assert_error(sStringToSize(wrap("-9223372036854775807")),
               "unable to convert negative value to size: \"-9223372036854775807\"");
  assert_error(sStringToSize(wrap("-9223372036854775808")),
               "unable to convert negative value to size: \"-9223372036854775808\"");
  assert_error(sStringToSize(wrap("-9223372036854775809")),
               "unable to convert negative value to size: \"-9223372036854775809\"");
  assert_error(sStringToSize(wrap("-99999999999999999999")),
               "unable to convert negative value to size: \"-99999999999999999999\"");

  assert_error(sStringToSize(wrap("")), "unable to convert to size: \"\"");
  assert_error(sStringToSize(wrap("foo")), "unable to convert to size: \"foo\"");
  assert_error(sStringToSize(wrap("  foo")), "unable to convert to size: \"  foo\"");
  assert_error(sStringToSize(wrap("ef68")), "unable to convert to size: \"ef68\"");
  assert_error(sStringToSize(wrap("--1")), "unable to convert to size: \"--1\"");
  assert_error(sStringToSize(wrap("++1")), "unable to convert to size: \"++1\"");
  testGroupEnd();

  testGroupStart("sTime()");
  assert_true(sTime() != (time_t)-1);
  testGroupEnd();

  testGroupStart("sDirOpen()");
  DirIterator *test_directory = sDirOpen(wrap("test directory"));
  assert_true(test_directory != NULL);

  DirIterator *test_foo_1 = sDirOpen(wrap("./test directory/foo 1/"));
  assert_true(test_foo_1 != NULL);

  assert_error_errno(sDirOpen(wrap("non-existing-directory")),
                     "failed to open directory \"non-existing-directory\"", ENOENT);
  testGroupEnd();

  testGroupStart("sDirGetNext()");
  /* Count example files in "test directory". */
  for(size_t counter = 0; counter < 17; counter++)
  {
    checkReadDir(test_directory);
  }

  assert_true(errno == 0);
  assert_true(sDirGetNext(test_directory).length == 0);
  assert_true(errno == 0);

  /* Count example files in "test directory/foo 1". */
  for(size_t counter = 0; counter < 5; counter++)
  {
    checkReadDir(test_foo_1);
  }

  assert_true(errno == 0);
  assert_true(sDirGetNext(test_foo_1).length == 0);
  assert_true(errno == 0);
  testGroupEnd();

  testGroupStart("sDirClose()");
  sDirClose(test_directory);
  sDirClose(test_foo_1);
  testGroupEnd();
}
