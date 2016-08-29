/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

/** @file
  Tests safe wrapper functions.
*/

#include "safe-wrappers.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

/** Calls sReadDir() with the given arguments and checks its result. This
  function asserts that errno doesn't get modified. Errno must be set to 0
  before this function can be called. */
static void checkReadDir(DIR *dir, const char *dir_path)
{
  assert_true(errno == 0);
  struct dirent *dir_entry = sReadDir(dir,dir_path);
  assert_true(errno == 0);

  assert_true(dir_entry != NULL);
  assert_true(strcmp(dir_entry->d_name, ".")  != 0);
  assert_true(strcmp(dir_entry->d_name, "..") != 0);
}

/** A wrapper around sPathExists() which asserts that errno doesn't get
  trashed. Errno must be 0 when this function gets called.
*/
static bool checkPathExists(const char *path)
{
  assert_true(errno == 0);
  bool path_exists = sPathExists(path);
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

/** Tests sReadLine().

  @param stream The stream which should be passed to sReadLine().
  @param expected_line The expected string.
*/
static void checkReadLine(FILE *stream, const char *expected_line)
{
  char *line = sReadLine(stream);
  assert_true(strcmp(line, expected_line) == 0);
  free(line);
}

/** Tests sReadLine() by reading lines from "valid-config-files/simple.txt"
  using the given file stream.

  @param stream A valid stream.
*/
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

  testGroupStart("sSizeAdd()");
  const char *expected_error = "overflow calculating object size";
  assert_true(sSizeAdd(0, 0) == 0);
  assert_true(sSizeAdd(2, 3) == 5);
  assert_true(sSizeAdd(50, 75) == 125);
  assert_true(sSizeAdd(65, SIZE_MAX - 65) == SIZE_MAX);
  assert_error(sSizeAdd(SIZE_MAX, SIZE_MAX), expected_error);
  assert_error(sSizeAdd(512, SIZE_MAX - 90), expected_error);
  assert_error(sSizeAdd(SIZE_MAX, 1), expected_error);
  testGroupEnd();

  testGroupStart("sSizeMul()");
  assert_true(sSizeMul(0, 5) == 0);
  assert_true(sSizeMul(5, 3) == 15);
  assert_true(sSizeMul(3, 5) == 15);
  assert_true(sSizeMul(70, 80) == 5600);
  assert_true(sSizeMul(SIZE_MAX, 1));
  assert_error(sSizeMul(SIZE_MAX, 25), expected_error);
  assert_error(sSizeMul(SIZE_MAX - 80, 295), expected_error);
  testGroupEnd();

  testGroupStart("sUint64Add()");
  const char *expected_error_u64 = "overflow calculating unsigned 64-bit value";
  assert_true(sUint64Add(0, 0) == 0);
  assert_true(sUint64Add(2, 3) == 5);
  assert_true(sUint64Add(50, 75) == 125);
  assert_true(sUint64Add(65, UINT64_MAX - 65) == UINT64_MAX);
  assert_error(sUint64Add(UINT64_MAX, UINT64_MAX), expected_error_u64);
  assert_error(sUint64Add(512, UINT64_MAX - 90), expected_error_u64);
  assert_error(sUint64Add(UINT64_MAX, 1), expected_error_u64);
  testGroupEnd();

  testGroupStart("sPathExists()");
  assert_error(sPathExists("empty.txt/foo"),
               "failed to check existence of \"empty.txt/foo\": Not a directory");
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

  assert_true(sPathExists("tmp/dummy-symlink") == false);
  assert_true(symlink("non-existing-file.txt", "tmp/dummy-symlink") == 0);
  assert_true(sPathExists("tmp/dummy-symlink"));
  assert_true(sPathExists("tmp/dummy-symlink/bar") == false);
  testGroupEnd();

  testGroupStart("sStat()");
  assert_error(sStat("non-existing-file.txt"), "failed to access "
               "\"non-existing-file.txt\": No such file or directory");

  struct stat example_stat = sStat("symlink.txt");
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("sLStat()");
  assert_error(sLStat("non-existing-file.txt"), "failed to access "
               "\"non-existing-file.txt\": No such file or directory");

  example_stat = sLStat("symlink.txt");
  assert_true(!S_ISREG(example_stat.st_mode));
  assert_true(S_ISLNK(example_stat.st_mode));

  example_stat = sLStat("example.txt");
  assert_true(S_ISREG(example_stat.st_mode));
  assert_true(example_stat.st_size == 25);
  testGroupEnd();

  testGroupStart("FileStream reading functions");
  assert_error(sFopenRead("non-existing-file.txt"),
               "failed to open \"non-existing-file.txt\" for reading: No such file or directory");

  const char *example_path = "example.txt";
  FileStream *example_read = sFopenRead(example_path);
  assert_true(example_read != NULL);

  assert_true(checkBytesLeft(example_read));
  assert_true(checkBytesLeft(example_read));

  char buffer[50] = { 0 };
  sFread(buffer, 25, example_read);

  assert_true(checkBytesLeft(example_read) == false);
  assert_true(checkBytesLeft(example_read) == false);

  assert_true(strcmp(buffer, "This is an example file.\n") == 0);

  assert_true(Fdestroy(example_read) == example_path);
  assert_true(errno == 0);

  /* Try reading 50 bytes from a 25 byte long file. */
  example_read = sFopenRead("example.txt");
  assert_true(example_read != NULL);
  assert_error(sFread(buffer, sizeof(buffer), example_read),
               "reading \"example.txt\": reached end of file unexpectedly");

  /* Provoke failure by reading from a write-only stream. */
  assert_error(sFread(buffer, 10, sFopenWrite("tmp/example-write")),
               "IO error while reading \"tmp/example-write\": Bad file descriptor");

  /* Test sFclose(). */
  example_read = sFopenRead("example.txt");
  assert_true(example_read != NULL);
  sFclose(example_read);

  /* Test sFbytesLeft(). */
  assert_error(sFbytesLeft(sFopenRead("test directory")),
               "failed to check for remaining bytes in \"test directory\": Is a directory");
  assert_error(sFbytesLeft(sFopenWrite("tmp/some-test-file.txt")),
               "failed to check for remaining bytes in \"tmp/some-test-file.txt\": Bad file descriptor");

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
  assert_error(sGetFilesContent("non-existing-file.txt"), "failed to "
               "access \"non-existing-file.txt\": No such file or "
               "directory");

  FileContent example_content = sGetFilesContent("example.txt");
  assert_true(example_content.size == 25);
  assert_true(example_content.content != NULL);
  assert_true(strncmp(example_content.content,
                      "This is an example file.\n", 25) == 0);
  free(example_content.content);

  FileContent empty_content = sGetFilesContent("empty.txt");
  assert_true(empty_content.size == 0);
  assert_true(empty_content.content == NULL);
  testGroupEnd();

  testGroupStart("FileStream writing functions");
  assert_error(sFopenWrite("non-existing-dir/file.txt"),
               "failed to open \"non-existing-dir/file.txt\" for writing: No such file or directory");

  assert_true(sPathExists("tmp/test-file-1") == false);
  FileStream *test_file = sFopenWrite("tmp/test-file-1");
  assert_true(sPathExists("tmp/test-file-1"));
  assert_true(test_file != NULL);

  sFwrite("hello", 5, test_file);
  assert_true(Fwrite(" ", 1, test_file) == true);
  assert_true(Ftodisk(test_file) == true);
  assert_true(Fwrite("world", 5, test_file) == true);
  sFwrite("!", 1, test_file);
  assert_true(Ftodisk(test_file) == true);
  assert_true(Ftodisk(test_file) == true);
  sFclose(test_file);

  FileContent test_file_1_content = sGetFilesContent("tmp/test-file-1");
  assert_true(test_file_1_content.size == 12);
  assert_true(memcmp(test_file_1_content.content, "hello world!", 12) == 0);
  free(test_file_1_content.content);

  /* Assert that the path gets captured properly. */
  const char *test_file_path = "tmp/test-file-2";

  assert_true(sPathExists(test_file_path) == false);
  test_file = sFopenWrite(test_file_path);
  assert_true(sPathExists(test_file_path));
  assert_true(test_file != NULL);

  assert_true(Fdestroy(test_file) == test_file_path);
  assert_true(errno == 0);

  FileContent test_file_2_content = sGetFilesContent("tmp/test-file-2");
  assert_true(test_file_2_content.size == 0);
  assert_true(test_file_2_content.content == NULL);

  /* Test overwriting behaviour. */
  test_file = sFopenWrite("tmp/test-file-1");
  assert_true(test_file != NULL);
  sFwrite("Test 1 2 3", 10, test_file);
  sFclose(test_file);

  FileContent test_file_content = sGetFilesContent("tmp/test-file-1");
  assert_true(test_file_content.size == 10);
  assert_true(memcmp(test_file_content.content, "Test 1 2 3", 10) == 0);
  free(test_file_content.content);

  /* Provoke errors by writing to a read-only stream. */
  assert_error(sFwrite("hello", 5, sFopenRead("example.txt")),
               "failed to write to \"example.txt\": Bad file descriptor");

  test_file = sFopenRead("example.txt");
  assert_true(Fwrite("hello", 5, test_file) == false);
  sFclose(test_file);
  testGroupEnd();

  testGroupStart("sMkdir()");
  assert_true(sPathExists("tmp/some-directory") == false);
  sMkdir("tmp/some-directory");
  assert_true(sPathExists("tmp/some-directory"));
  assert_true(S_ISDIR(sLStat("tmp/some-directory").st_mode));

  assert_error(sMkdir("tmp/some-directory"),
               "failed to create directory: \"tmp/some-directory\": File exists");
  assert_error(sMkdir("tmp/non-existing/foo"),
               "failed to create directory: \"tmp/non-existing/foo\": No such file or directory");
  testGroupEnd();

  testGroupStart("sSymlink()");
  assert_true(sPathExists("tmp/some-symlink") == false);
  sSymlink("foo bar 123", "tmp/some-symlink");
  assert_true(sPathExists("tmp/some-symlink"));
  assert_true(S_ISLNK(sLStat("tmp/some-symlink").st_mode));

  char some_symlink_buffer[12] = { 0 };
  assert_true(11 == readlink("tmp/some-symlink", some_symlink_buffer, sizeof(some_symlink_buffer)));
  assert_true(strcmp(some_symlink_buffer, "foo bar 123") == 0);

  assert_error(sSymlink("test", "tmp/some-symlink"),
               "failed to create symlink: \"tmp/some-symlink\": File exists");
  assert_error(sSymlink("backup", "tmp/non-existing/bar"),
               "failed to create symlink: \"tmp/non-existing/bar\": No such file or directory");
  testGroupEnd();

  testGroupStart("sRename()");
  assert_true(sPathExists("tmp/file-1") == false);
  sFclose(sFopenWrite("tmp/file-1"));

  assert_true(sPathExists("tmp/file-1"));
  assert_true(sPathExists("tmp/file-2") == false);

  sRename("tmp/file-1", "tmp/file-2");

  assert_true(sPathExists("tmp/file-1") == false);
  assert_true(sPathExists("tmp/file-2"));

  assert_error(sRename("non-existing-file.txt", "tmp/file-2"),
               "failed to rename \"non-existing-file.txt\" to \"tmp/file-2\": No such file or directory");

  assert_true(sPathExists("tmp/file-2"));
  assert_true(sStat("tmp/file-2").st_size == 0);
  testGroupEnd();

  testGroupStart("sChmod()");
  sChmod("tmp/test-file-1", 0600);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0600);
  sChmod("tmp/test-file-1", 0404);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0404);
  sChmod("tmp/test-file-1", 0544);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0544);
  sChmod("tmp/test-file-1", 0644);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0644);

  sSymlink("test-file-1", "tmp/test-symlink-1");
  sChmod("tmp/test-symlink-1", 0600);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0600);
  sChmod("tmp/test-symlink-1", 0404);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0404);
  sChmod("tmp/test-symlink-1", 0544);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0544);
  sChmod("tmp/test-symlink-1", 0644);
  assert_true((sLStat("tmp/test-file-1").st_mode & ~S_IFMT) == 0644);

  assert_error(sChmod("tmp/non-existing", 0600),
               "failed to change permissions of \"tmp/non-existing\": No such file or directory");
  testGroupEnd();

  testGroupStart("sChown()");
  struct stat test_file_1_stat = sLStat("tmp/test-file-1");
  sChown("tmp/test-file-1", test_file_1_stat.st_uid, test_file_1_stat.st_gid);

  sSymlink("non-existing", "tmp/dangling-symlink");
  assert_error(sChown("tmp/dangling-symlink", test_file_1_stat.st_uid, test_file_1_stat.st_gid),
               "failed to change owner of \"tmp/dangling-symlink\": No such file or directory");
  testGroupEnd();

  testGroupStart("sLChown()");
  struct stat dangling_symlink_stat = sLStat("tmp/dangling-symlink");

  sLChown("tmp/dangling-symlink", dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid);

  assert_error(sLChown("tmp/non-existing", dangling_symlink_stat.st_uid, dangling_symlink_stat.st_gid),
               "failed to change owner of \"tmp/non-existing\": No such file or directory");
  testGroupEnd();

  testGroupStart("sUtime()");
  sUtime("tmp/test-file-1", 123);
  assert_true(sLStat("tmp/test-file-1").st_mtime == 123);
  sUtime("tmp/test-file-1", 987654);
  assert_true(sLStat("tmp/test-file-1").st_mtime == 987654);
  sUtime("tmp/test-file-1", 555);
  assert_true(sLStat("tmp/test-file-1").st_mtime == 555);
  sUtime("tmp/test-symlink-1", 13579);
  assert_true(sLStat("tmp/test-file-1").st_mtime == 13579);
  sUtime("tmp/test-symlink-1", 900);
  assert_true(sLStat("tmp/test-file-1").st_mtime == 900);
  sUtime("tmp/test-symlink-1", 12);
  assert_true(sLStat("tmp/test-file-1").st_mtime == 12);

  assert_error(sUtime("tmp/non-existing", 123),
               "failed to set timestamp of \"tmp/non-existing\": No such file or directory");
  testGroupEnd();

  testGroupStart("sRemove()");
  sFclose(sFopenWrite("tmp/file-to-remove"));
  sMkdir("tmp/dir-to-remove");
  sSymlink("file-to-remove", "tmp/link-to-remove1");
  sSymlink("dir-to-remove", "tmp/link-to-remove2");

  sRemove("tmp/link-to-remove1");
  sRemove("tmp/link-to-remove2");
  assert_true(sPathExists("tmp/file-to-remove"));
  assert_true(sPathExists("tmp/dir-to-remove"));
  assert_true(sPathExists("tmp/link-to-remove1") == false);
  assert_true(sPathExists("tmp/link-to-remove2") == false);

  sRemove("tmp/file-to-remove");
  assert_true(sPathExists("tmp/file-to-remove") == false);

  sRemove("tmp/dir-to-remove");
  assert_true(sPathExists("tmp/dir-to-remove") == false);

  assert_error(sRemove("tmp/non-existing"),
               "failed to remove \"tmp/non-existing\": No such file or directory");
  assert_error(sRemove("tmp/non-existing-dir/foo"),
               "failed to remove \"tmp/non-existing-dir/foo\": No such file or directory");

  sMkdir("tmp/non-empty-dir");
  sFclose(sFopenWrite("tmp/non-empty-dir/foo"));
  assert_error(sRemove("tmp/non-empty-dir"),
               "failed to remove \"tmp/non-empty-dir\": Directory not empty");

  sRemove("tmp/non-empty-dir/foo");
  sRemove("tmp/non-empty-dir");
  assert_true(sPathExists("tmp/non-empty-dir") == false);
  testGroupEnd();

  testGroupStart("sRemoveRecursively()");
  assert_true(sPathExists("tmp/test-file-1"));
  assert_true(sPathExists("tmp/test-symlink-1"));
  sRemoveRecursively("tmp/test-symlink-1");
  assert_true(sPathExists("tmp/test-file-1"));
  assert_true(sPathExists("tmp/test-symlink-1") == false);

  sRemoveRecursively("tmp/test-file-1");
  assert_true(sPathExists("tmp/test-file-1") == false);

  sMkdir("tmp/foo");
  sFclose(sFopenWrite("tmp/foo/bar"));
  sSymlink("bar", "tmp/foo/123");
  sMkdir("tmp/foo/1");
  sMkdir("tmp/foo/1/2");
  sMkdir("tmp/foo/1/2/3");
  sMkdir("tmp/foo/1/2/3/4");
  sMkdir("tmp/foo/1/2/3/4/5");
  sMkdir("tmp/foo/1/2/3/4/6");
  sMkdir("tmp/foo/1/2/3/4/7");
  sMkdir("tmp/foo/1/2/3/xyz");
  sSymlink("../../../..", "tmp/foo/1/2/3/abc");
  sSymlink("../../../bar", "tmp/foo/1/2/bar");
  sFclose(sFopenWrite("tmp/bar"));

  assert_true(sPathExists("tmp/foo"));
  assert_true(sPathExists("tmp/bar"));
  sRemoveRecursively("tmp/foo");
  assert_true(sPathExists("tmp/foo") == false);
  assert_true(sPathExists("tmp/bar"));

  sRemoveRecursively("tmp/bar");
  assert_true(sPathExists("tmp/bar") == false);

  assert_error(sRemoveRecursively(""),
               "failed to access \"\": No such file or directory");
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

  testGroupStart("sStringToSize()");
  errno = 7;

  assert_true(sStringToSize("0") == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize("55") == 55);
  assert_true(errno == 7);
  assert_true(sStringToSize("100982") == 100982);
  assert_true(errno == 7);
  assert_true(sStringToSize("   53") == 53);
  assert_true(errno == 7);
  assert_true(sStringToSize("+129") == 129);
  assert_true(errno == 7);
  assert_true(sStringToSize("0x17") == 0);
  assert_true(errno == 7);
  assert_true(sStringToSize("92a7ff") == 92);
  assert_true(errno == 7);
  assert_true(sStringToSize("0777") == 777);
  assert_true(errno == 7);
  assert_true(sStringToSize("01938") == 1938);
  assert_true(errno == 7);
  assert_true(sStringToSize("28.7") == 28);
  assert_true(errno == 7);
  assert_true(sStringToSize("34,6") == 34);
  assert_true(errno == 7);
  assert_true(sStringToSize("4294967295") == 4294967295);
  assert_true(errno == 7);

#if SIZE_MAX == UINT32_MAX
  assert_error(sStringToSize("4294967296"),
               "value too large to convert to size: \"4294967296\"");
#elif SIZE_MAX == UINT64_MAX
  assert_true(sStringToSize("9223372036854775807") == 9223372036854775807);
  assert_true(errno == 7);
#endif

  assert_error(sStringToSize("9223372036854775808"),
               "value too large to convert to size: \"9223372036854775808\"");

  assert_error(sStringToSize("-1"),
               "unable to convert negative value to size: \"-1\"");
  assert_error(sStringToSize("-100964"),
               "unable to convert negative value to size: \"-100964\"");
  assert_error(sStringToSize("-4294967295"),
               "unable to convert negative value to size: \"-4294967295\"");
  assert_error(sStringToSize("-4294967296"),
               "unable to convert negative value to size: \"-4294967296\"");
  assert_error(sStringToSize("-9223372036854775807"),
               "unable to convert negative value to size: \"-9223372036854775807\"");
  assert_error(sStringToSize("-9223372036854775808"),
               "unable to convert negative value to size: \"-9223372036854775808\"");
  assert_error(sStringToSize("-9223372036854775809"),
               "unable to convert negative value to size: \"-9223372036854775809\"");
  assert_error(sStringToSize("-99999999999999999999"),
               "unable to convert negative value to size: \"-99999999999999999999\"");

  assert_error(sStringToSize(""),      "unable to convert to size: \"\"");
  assert_error(sStringToSize("foo"),   "unable to convert to size: \"foo\"");
  assert_error(sStringToSize("  foo"), "unable to convert to size: \"  foo\"");
  assert_error(sStringToSize("ef68"),  "unable to convert to size: \"ef68\"");
  assert_error(sStringToSize("--1"),   "unable to convert to size: \"--1\"");
  assert_error(sStringToSize("++1"),   "unable to convert to size: \"++1\"");
  testGroupEnd();

  testGroupStart("sTime()");
  assert_true(sTime() != (time_t)-1);
  testGroupEnd();

  testGroupStart("sOpenDir()");
  DIR *test_directory = sOpenDir("test directory");
  assert_true(test_directory != NULL);

  DIR *test_foo_1 = sOpenDir("./test directory/foo 1/");
  assert_true(test_foo_1 != NULL);

  assert_error(sOpenDir("non-existing-directory"),
               "failed to open directory \"non-existing-directory\": No such file or directory");
  testGroupEnd();

  testGroupStart("sReadDir()");
  /* Count example files in "test directory". */
  for(size_t counter = 0; counter < 17; counter++)
  {
    checkReadDir(test_directory, "test directory");
  }

  assert_true(errno == 0);
  assert_true(sReadDir(test_directory, "test directory") == NULL);
  assert_true(errno == 0);

  /* Count example files in "test directory/foo 1". */
  for(size_t counter = 0; counter < 5; counter++)
  {
    checkReadDir(test_foo_1, "test directory/foo 1");
  }

  assert_true(errno == 0);
  assert_true(sReadDir(test_foo_1, "test directory/foo 1") == NULL);
  assert_true(errno == 0);
  testGroupEnd();

  testGroupStart("sCloseDir()");
  sCloseDir(test_directory, "test directory");
  sCloseDir(test_foo_1, "test directory/foo 1");
  testGroupEnd();
}
