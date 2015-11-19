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
  Tests functions for printing colorized text.
*/

#include "colors.h"

#include <errno.h>
#include <stdlib.h>

#include "test.h"
#include "safe-wrappers.h"

int main(void)
{
  testGroupStart("colorPrintf()");
  /* Errno gets checked to ensure that colorPrintf() doesn't mess with it
     while testing streams for color support. Additionally it asserts that
     printf doesn't fail. */
  assert_true(errno == 0);

  /* Test file 1. */
  FILE *file_1 = fopen("tmp/file-1", "wb");
  assert_true(file_1 != NULL);

  colorPrintf(file_1, TC_green_bold, "This is a test");
  assert_true(errno == 0);
  colorPrintf(file_1, TC_red, " file");
  assert_true(errno == 0);
  colorPrintf(file_1, TC_blue, ".");
  assert_true(errno == 0);
  fclose(file_1);

  FileContent file_1_content = sGetFilesContent("tmp/file-1");
  assert_true(file_1_content.size == 20);
  assert_true(memcmp(file_1_content.content, "This is a test file.", file_1_content.size) == 0);
  free(file_1_content.content);

  /* Test file 2. */
  FILE *file_2 = fopen("tmp/file-2", "wb");
  assert_true(file_2 != NULL);

  colorPrintf(file_2, TC_yellow, "Hello");
  assert_true(errno == 0);
  colorPrintf(file_2, TC_yellow, " ");
  assert_true(errno == 0);
  colorPrintf(file_2, TC_green, "world");
  assert_true(errno == 0);
  colorPrintf(file_2, TC_red, ".");
  assert_true(errno == 0);
  fclose(file_2);

  FileContent file_2_content = sGetFilesContent("tmp/file-2");
  assert_true(file_2_content.size == 12);
  assert_true(memcmp(file_2_content.content, "Hello world.", file_2_content.size) == 0);
  free(file_2_content.content);
  testGroupEnd();
}
