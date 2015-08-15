/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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

/** Calls sReadDir() with the given arguments and checks its result. */
static void checkReadDir(DIR *dir, const char *dir_path)
{
  struct dirent *dir_entry = sReadDir(dir,dir_path);
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

  testGroupStart("sFopenRead()");
  FILE *example_txt_1 = sFopenRead("example.txt");
  assert_true(example_txt_1 != NULL);

  FILE *example_txt_2 = sFopenRead("example.txt");
  assert_true(example_txt_2 != NULL);

  FILE *example_txt_3 = sFopenRead("example.txt");
  assert_true(example_txt_3 != NULL);

  assert_error(sFopenRead("non-existing-file.txt"),
               "failed to open \"non-existing-file.txt\" for reading: No such file or directory");
  testGroupEnd();

  testGroupStart("sFopenWrite()");
  FILE *dev_null = sFopenWrite("/dev/null");
  assert_true(dev_null != NULL);

  assert_error(sFopenWrite("non-existing-dir/file.txt"),
               "failed to open \"non-existing-dir/file.txt\" for writing: No such file or directory");
  testGroupEnd();

  testGroupStart("sFread()");
  char *example = sMalloc(25);
  sFread(example, 25, example_txt_2, "example.txt");

  assert_true(strncmp(example, "This is an example file.\n", 25) == 0);
  free(example);

  /* Try reading 50 bytes from a 25 byte long file. */
  example = sMalloc(50);
  assert_error(sFread(example, 50, example_txt_3, "example.txt"),
               "reading \"example.txt\": reached end of file unexpectedly");
  free(example);
  testGroupEnd();

  testGroupStart("sFclose()");
  sFclose(example_txt_1, "example.txt");
  sFclose(example_txt_2, "example.txt");
  sFclose(example_txt_3, "example.txt");
  sFclose(dev_null, "/dev/null");
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
  assert_true(sReadDir(test_directory, "test directory") == NULL);

  /* Count example files in "test directory/foo 1". */
  for(size_t counter = 0; counter < 5; counter++)
  {
    checkReadDir(test_foo_1, "test directory/foo 1");
  }
  assert_true(sReadDir(test_foo_1, "test directory/foo 1") == NULL);
  testGroupEnd();

  testGroupStart("sCloseDir()");
  sCloseDir(test_directory, "test directory");
  sCloseDir(test_foo_1, "test directory/foo 1");
  testGroupEnd();

  testGroupStart("sPathExists()");
  errno = 0;
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
}
