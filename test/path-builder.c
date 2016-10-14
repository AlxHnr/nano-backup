/*
  Copyright (c) 2016 Alexander Heinrich

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
  Tests functions for building reusable paths based on Buffer.
*/

#include "path-builder.h"

#include "test.h"

/** Tests pathBuilderSet(). */
static void testPathBuilderSet(Buffer **buffer, const char *path)
{
  size_t length = pathBuilderSet(buffer, path);

  assert_true(length == strlen(path));
  assert_true((*buffer)->data[length] == '\0');
  assert_true((*buffer)->capacity >= length + 1);

  assert_true(strlen((*buffer)->data) == length);
  assert_true(strcmp((*buffer)->data, path) == 0);
}

/** Tests pathBuilderAppend(). */
static void testPathBuilderAppend(Buffer **buffer,
                                  size_t buffer_length,
                                  const char *path,
                                  const char *expected_path)
{
  size_t length = pathBuilderAppend(buffer, buffer_length, path);

  assert_true(length == buffer_length + 1 + strlen(path));
  assert_true(length == strlen(expected_path));
  assert_true((*buffer)->data[length] == '\0');
  assert_true((*buffer)->capacity >= length + 1);

  assert_true(strlen((*buffer)->data) == length);
  assert_true(strcmp((*buffer)->data, expected_path) == 0);
}

int main(void)
{
  testGroupStart("pathBuilderSet()");
  Buffer *buffer = NULL;
  testPathBuilderSet(&buffer, "");
  testPathBuilderSet(&buffer, "foo");
  testPathBuilderSet(&buffer, "");
  testPathBuilderSet(&buffer, "foo/bar/super/long/path");
  testPathBuilderSet(&buffer, "abcdefghijkl");
  testPathBuilderSet(&buffer, "");
  testPathBuilderSet(&buffer, "foo/b");
  testPathBuilderSet(&buffer, "bar");

  buffer = NULL;
  testPathBuilderSet(&buffer, "foo/bar/super/long/path");
  testPathBuilderSet(&buffer, "abcdefghijkl");
  testPathBuilderSet(&buffer, "");
  testPathBuilderSet(&buffer, "foo/b");
  testPathBuilderSet(&buffer, "bar");
  testGroupEnd();

  testGroupStart("pathBuilderAppend()");
  buffer = NULL;
  testPathBuilderAppend(&buffer, 0, "/random/path/", "//random/path/");
  testPathBuilderAppend(&buffer, 0, "abc", "/abc");
  testPathBuilderAppend(&buffer, 0, "nano backup", "/nano backup");
  testPathBuilderAppend(&buffer, 5, "xyz", "/nano/xyz");
  testPathBuilderAppend(&buffer, 8, "/foo/bar/backup", "/nano/xy//foo/bar/backup");
  testPathBuilderAppend(&buffer, 5, "subdirectory", "/nano/subdirectory");
  testPathBuilderAppend(&buffer, 9, "path", "/nano/sub/path");
  testPathBuilderAppend(&buffer, 1, "12345", "//12345");
  testPathBuilderAppend(&buffer, 0, "12345", "/12345");

  testPathBuilderSet(&buffer, "test");
  testPathBuilderAppend(&buffer, 4, "path", "test/path");
  testPathBuilderAppend(&buffer, 9, "builder", "test/path/builder");
  testPathBuilderAppend(&buffer, 17, "implementation", "test/path/builder/implementation");

  buffer = NULL;
  testPathBuilderAppend(&buffer, 0, "test", "/test");
  testPathBuilderAppend(&buffer, 5, "path", "/test/path");
  testPathBuilderAppend(&buffer, 10, "builder", "/test/path/builder");
  testPathBuilderAppend(&buffer, 18, "implementation", "/test/path/builder/implementation");

  buffer = NULL;
  testPathBuilderSet(&buffer, "../..");
  testPathBuilderAppend(&buffer, 5, "test", "../../test");
  testPathBuilderAppend(&buffer, 10, "path", "../../test/path");
  testPathBuilderAppend(&buffer, 15, "builder", "../../test/path/builder");
  testPathBuilderAppend(&buffer, 23, "implementation", "../../test/path/builder/implementation");

  testPathBuilderSet(&buffer, "/");
  testPathBuilderAppend(&buffer, 0, "etc", "/etc");
  testPathBuilderAppend(&buffer, 4, "portage", "/etc/portage");
  testPathBuilderAppend(&buffer, 12, "make.conf", "/etc/portage/make.conf");

  testPathBuilderSet(&buffer, "tmp/file");
  testPathBuilderAppend(&buffer, 8,  "",  "tmp/file/");
  testPathBuilderAppend(&buffer, 8,  "",  "tmp/file/");
  testPathBuilderAppend(&buffer, 9,  "",  "tmp/file//");
  testPathBuilderAppend(&buffer, 10, "",  "tmp/file///");
  testPathBuilderAppend(&buffer, 8,  "a", "tmp/file/a");
  testPathBuilderAppend(&buffer, 10, "a", "tmp/file/a/a");
  testPathBuilderAppend(&buffer, 12, "a", "tmp/file/a/a/a");
  testPathBuilderAppend(&buffer, 7,  "",  "tmp/fil/");
  testGroupEnd();
}
