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
  Tests various helper functions for handling repository.
*/

#include "repository.h"

#include "test.h"
#include "safe-wrappers.h"

/** Creates the given filepath. */
static void createFile(const char *path)
{
  sFclose(sFopenWrite(path));
}

int main(void)
{
  testGroupStart("repoRegularFileExists()");
  String repo_path = str("tmp");

  RegularFileInfo info_1 =
  {
    .size = 139, .slot = 24,
    .hash =
    {
      0x07, 0x0a, 0x0d, 0x10, 0x13, 0x16, 0x19, 0x1c, 0x1f, 0x22,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };
  assert_true(repoRegularFileExists(repo_path, &info_1) == false);
  createFile("tmp/070a0d101316191c1f2225282b2e3134373a3d40:139:24");
  assert_true(repoRegularFileExists(repo_path, &info_1) == true);

  RegularFileInfo info_2 =
  {
    .size = 138904, .slot = 255,
    .hash =
    {
      0x21, 0x51, 0x4d, 0x1d, 0x49, 0x15, 0x19, 0x41, 0x39, 0x3d,
      0x2d, 0x25, 0x11, 0x09, 0x55, 0x29, 0x31, 0x35, 0x0d, 0x45,
    },
  };
  assert_true(repoRegularFileExists(repo_path, &info_2) == false);
  createFile("tmp/21514d1d49151941393d2d251109552931350d45:138904:255");
  assert_true(repoRegularFileExists(repo_path, &info_2) == true);

  RegularFileInfo info_3 =
  {
    .size = 18446744073709551615UL, .slot = 0,
    .hash =
    {
      0x4b, 0x5f, 0x2b, 0x13, 0x4f, 0x47, 0x3b, 0x1f, 0x27, 0x57,
      0x33, 0x3f, 0x17, 0x53, 0x1b, 0x23, 0x37, 0x2f, 0x43, 0x5b,
    },
  };
  assert_true(repoRegularFileExists(repo_path, &info_3) == false);
  createFile("tmp/4b5f2b134f473b1f2757333f17531b23372f435b:18446744073709551615:0");
  assert_true(repoRegularFileExists(repo_path, &info_3) == true);

  RegularFileInfo info_4 =
  {
    .size = 0, .slot = 39,
    .hash =
    {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
    },
  };
  assert_true(repoRegularFileExists(repo_path, &info_4) == false);
  createFile("tmp/000000000000112233445566778899aabbccddee:0:39");
  assert_true(repoRegularFileExists(repo_path, &info_4) == true);
  testGroupEnd();
}
