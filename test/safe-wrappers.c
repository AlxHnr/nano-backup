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

/**
  @file safe-wrappers.c Tests safe wrapper functions.
*/

#include "safe-wrappers.h"

#include <stdlib.h>
#include <stdint.h>

#include "test.h"

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
  assert_error(sFopenRead("non-existing-file.txt"),
               "failed to open \"non-existing-file.txt\" for reading: "
               "No such file or directory");
  FILE *file = sFopenRead("example.txt");
  assert_true(file != NULL);
  sFclose(file, "example.txt");
  testGroupEnd();

  testGroupStart("sFopenWrite()");
  assert_error(sFopenWrite("non-existing-dir/file.txt"),
               "failed to open \"non-existing-dir/file.txt\" for writing: "
               "No such file or directory");
  file = sFopenWrite("/dev/null");
  assert_true(file != NULL);
  sFclose(file, "/dev/null");
  testGroupEnd();

  testGroupStart("sFread()");
  char *example = sMalloc(25);

  file = sFopenRead("example.txt");
  sFread(example, 25, file, "example.txt");
  sFclose(file, "example.txt");

  assert_true(strncmp(example, "This is an example file.\n", 25) == 0);
  free(example);

  /* Try reading 50 bytes from a 25 byte long file. */
  example = sMalloc(50);
  file = sFopenRead("example.txt");
  assert_error(sFread(example, 50, file, "example.txt"), "reading "
               "\"example.txt\": reached end of file unexpectedly");
  sFclose(file, "example.txt");
  free(example);
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
