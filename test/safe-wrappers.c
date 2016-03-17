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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
