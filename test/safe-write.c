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
  Test safe writing of files.
*/

#include "safe-write.h"

#include <stdlib.h>

#include "test.h"
#include "safe-wrappers.h"

static void checkTestFile(void)
{
  FileContent content = sGetFilesContent("tmp/test.txt");
  assert_true(content.size == 13);
  assert_true(memcmp(content.content, "Hello backup!", 13) == 0);
  free(content.content);
}

int main(void)
{
  testGroupStart("openSafeWriteHandle()");
  assert_error(openSafeWriteHandle("non-existing-directory", "foo", "bar"),
               "failed to open \"non-existing-directory/tmp-file\" for writing: No such file or directory");
  assert_error(openSafeWriteHandle("example.txt", "foo", "bar"),
               "failed to open \"example.txt/tmp-file\" for writing: Not a directory");

  assert_true(sPathExists("tmp/tmp-file") == false);
  assert_true(sPathExists("tmp/test.txt") == false);

  SafeWriteHandle *handle =
    openSafeWriteHandle("tmp", "test.txt", "test.txt");
  assert_true(handle != NULL);

  assert_true(sPathExists("tmp/tmp-file") == true);
  assert_true(sPathExists("tmp/test.txt") == false);
  testGroupEnd();

  testGroupStart("writeSafeWriteHandle()");
  writeSafeWriteHandle(handle, "Hello",  5);
  writeSafeWriteHandle(handle, " ",      1);
  writeSafeWriteHandle(handle, "backup", 6);
  writeSafeWriteHandle(handle, "!",      1);
  testGroupEnd();

  testGroupStart("closeSafeWriteHandle()");
  assert_true(sPathExists("tmp/tmp-file") == true);
  assert_true(sPathExists("tmp/test.txt") == false);

  closeSafeWriteHandle(handle);

  assert_true(sPathExists("tmp/tmp-file") == false);
  assert_true(sPathExists("tmp/test.txt") == true);
  checkTestFile();
  testGroupEnd();

  testGroupStart("safe overwriting");
  handle = openSafeWriteHandle("tmp", "test.txt", "test.txt");
  assert_true(sPathExists("tmp/tmp-file") == true);
  assert_true(sPathExists("tmp/test.txt") == true);
  assert_true(handle != NULL);
  checkTestFile();

  writeSafeWriteHandle(handle, "This",  4);
  writeSafeWriteHandle(handle, " is",   3);
  writeSafeWriteHandle(handle, " a ",   3);
  writeSafeWriteHandle(handle, "test.", 5);
  checkTestFile();

  assert_true(sPathExists("tmp/tmp-file") == true);
  assert_true(sPathExists("tmp/test.txt") == true);
  closeSafeWriteHandle(handle);
  assert_true(sPathExists("tmp/tmp-file") == false);
  assert_true(sPathExists("tmp/test.txt") == true);

  FileContent content = sGetFilesContent("tmp/test.txt");
  assert_true(content.size == 15);
  assert_true(memcmp(content.content, "This is a test.", 15) == 0);
  free(content.content);
  testGroupEnd();

  testGroupStart("behaviour with existing tmp-file");
  sRename("tmp/test.txt", "tmp/tmp-file");
  assert_true(sStat("tmp/tmp-file").st_size == 15);

  handle = openSafeWriteHandle("tmp", "foo.txt", "foo.txt");
  writeSafeWriteHandle(handle, "Nano Backup", 11);
  closeSafeWriteHandle(handle);

  assert_true(sPathExists("tmp/tmp-file") == false);
  assert_true(sPathExists("tmp/foo.txt")  == true);

  FileContent foo_content = sGetFilesContent("tmp/foo.txt");
  assert_true(foo_content.size == 11);
  assert_true(memcmp(foo_content.content, "Nano Backup", 11) == 0);
  free(foo_content.content);
  testGroupEnd();

  testGroupStart("overwrite with empty file");
  closeSafeWriteHandle(openSafeWriteHandle("tmp", "foo.txt", "foo.txt"));
  assert_true(sPathExists("tmp/tmp-file") == false);
  assert_true(sPathExists("tmp/foo.txt")  == true);
  assert_true(sStat("tmp/foo.txt").st_size == 0);
  testGroupEnd();
}
