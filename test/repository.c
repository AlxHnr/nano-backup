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

#include <stdio.h>
#include <stdlib.h>

#include "test.h"
#include "safe-wrappers.h"

#define TMP_FILE_PATH "tmp/tmp-file"

/** Tests repoRegularFileExists() by creating the specific file.

  @param file_path The path of the file which will be created.
  @param info Informations describing the file.
*/
static void testFileExists(const char *file_path,
                           const RegularFileInfo *info)
{
  String repo_path = str("tmp");

  assert_true(repoRegularFileExists(repo_path, info) == false);
  sFclose(sFopenWrite(file_path));
  assert_true(repoRegularFileExists(repo_path, info) == true);
  assert_true(remove(file_path) == 0);
  assert_true(repoRegularFileExists(repo_path, info) == false);
  assert_true(sPathExists(file_path) == false);
}

/** Asserts that the given file contains the string "Hello backup!". */
static void checkTestFile(const char *file_path)
{
  FileContent content = sGetFilesContent(file_path);
  assert_true(content.size == 13);
  assert_true(memcmp(content.content, "Hello backup!", 13) == 0);
  free(content.content);
}

/** Tests writing with the given RepoWriter.

  @param writer A new, unused RepoWriter. It will be destroyed by this
  function, so it should not be used again.
  @param final_path The file which should be created by finalizing the
  given RepoWriter.
*/
static void testRepoWriter(RepoWriter *writer, const char *final_path)
{
  assert_true(writer != NULL);
  assert_true(sPathExists(TMP_FILE_PATH) == true);
  assert_true(sPathExists(final_path)    == false);

  repoWriterWrite("Hello",  5, writer);
  repoWriterWrite(" ",      1, writer);
  repoWriterWrite("backup", 6, writer);
  repoWriterWrite("!",      1, writer);

  assert_true(sPathExists(TMP_FILE_PATH) == true);
  assert_true(sPathExists(final_path)    == false);

  repoWriterClose(writer);

  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(final_path)    == true);
  checkTestFile(final_path);
}

/** Overwrites the given filepath with the file represented by the
  RepoWriter.

  @param writer A new, unused writer used for overwriting the final
  filepath. It will be destroyed by this function.
  @param final_path The file which should be overwritten by this test.
*/
static void testSafeOverwriting(RepoWriter *writer, const char *final_path)
{
  assert_true(sPathExists(TMP_FILE_PATH) == true);
  assert_true(sPathExists(final_path)    == true);
  assert_true(writer != NULL);
  checkTestFile(final_path);

  repoWriterWrite("This",  4, writer);
  repoWriterWrite(" is",   3, writer);
  repoWriterWrite(" a ",   3, writer);
  repoWriterWrite("test.", 5, writer);
  checkTestFile(final_path);

  assert_true(sPathExists(TMP_FILE_PATH) == true);
  assert_true(sPathExists(final_path)    == true);
  repoWriterClose(writer);
  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(final_path)    == true);

  FileContent content = sGetFilesContent(final_path);
  assert_true(content.size == 15);
  assert_true(memcmp(content.content, "This is a test.", content.size) == 0);
  free(content.content);
}

/** Tests the given RepoWriter. The repositories temporary file must have
  existed before the writer was opened.

  @param writer A new, unused RepoWriter. It will be destroyed by this
  function.
  @param final_path The file which should be created by finalizing the
  given RepoWriter.
*/
static void testWithExistingTmpFile(RepoWriter *writer,
                                    const char *final_path)
{
  assert_true(sPathExists(TMP_FILE_PATH) == true);
  assert_true(sPathExists(final_path)    == false);

  repoWriterWrite("Nano Backup", 11, writer);
  repoWriterClose(writer);

  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(final_path)    == true);

  FileContent content = sGetFilesContent(final_path);
  assert_true(content.size == 11);
  assert_true(memcmp(content.content, "Nano Backup", content.size) == 0);
  free(content.content);
}

int main(void)
{
  const char *info_1_path = "tmp/24-070a0d101316191c1f2225282b2e3134373a3d40-139";
  RegularFileInfo info_1 =
  {
    .size = 139, .slot = 24,
    .hash =
    {
      0x07, 0x0a, 0x0d, 0x10, 0x13, 0x16, 0x19, 0x1c, 0x1f, 0x22,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };

  const char *info_2_path = "tmp/255-21514d1d49151941393d2d251109552931350d45-138904";
  RegularFileInfo info_2 =
  {
    .size = 138904, .slot = 255,
    .hash =
    {
      0x21, 0x51, 0x4d, 0x1d, 0x49, 0x15, 0x19, 0x41, 0x39, 0x3d,
      0x2d, 0x25, 0x11, 0x09, 0x55, 0x29, 0x31, 0x35, 0x0d, 0x45,
    },
  };

  const char *info_3_path = "tmp/0-4b5f2b134f473b1f2757333f17531b23372f435b-18446744073709551615";
  RegularFileInfo info_3 =
  {
    .size = 18446744073709551615UL, .slot = 0,
    .hash =
    {
      0x4b, 0x5f, 0x2b, 0x13, 0x4f, 0x47, 0x3b, 0x1f, 0x27, 0x57,
      0x33, 0x3f, 0x17, 0x53, 0x1b, 0x23, 0x37, 0x2f, 0x43, 0x5b,
    },
  };

  const char *info_4_path = "tmp/39-000000000000112233445566778899aabbccddee-0";
  RegularFileInfo info_4 =
  {
    .size = 0, .slot = 39,
    .hash =
    {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
    },
  };

  testGroupStart("repoRegularFileExists()");
  assert_true(repoRegularFileExists(str("non-existing-path"), &info_1) == false);
  testFileExists(info_1_path, &info_1);
  testFileExists(info_2_path, &info_2);
  testFileExists(info_3_path, &info_3);
  testFileExists(info_4_path, &info_4);
  testGroupEnd();

  testGroupStart("write regular files to repository");
  assert_error(repoWriterOpenFile("non-existing-directory", "non-existing-directory/tmp-file", "foo", &info_1),
               "failed to open \"non-existing-directory/tmp-file\" for writing: No such file or directory");
  assert_error(repoWriterOpenFile("example.txt", "example.txt/tmp-file", "foo", &info_2),
               "failed to open \"example.txt/tmp-file\" for writing: Not a directory");

  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(info_1_path)   == false);
  testRepoWriter(repoWriterOpenFile("tmp", TMP_FILE_PATH, "info_1", &info_1), info_1_path);
  testGroupEnd();

  testGroupStart("write to repository in raw mode");
  assert_error(repoWriterOpenRaw("non-existing-directory", "non-existing-directory/tmp-file", "foo", "tmp/foo"),
               "failed to open \"non-existing-directory/tmp-file\" for writing: No such file or directory");
  assert_error(repoWriterOpenRaw("example.txt", "example.txt/tmp-file", "bar", "tmp/bar"),
               "failed to open \"example.txt/tmp-file\" for writing: Not a directory");

  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists("some-file")   == false);
  testRepoWriter(repoWriterOpenRaw("tmp", TMP_FILE_PATH, "some-file", "tmp/some-file"), "tmp/some-file");
  testGroupEnd();

  testGroupStart("safe overwriting");
  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(info_1_path)   == true);
  testSafeOverwriting(repoWriterOpenFile("tmp", TMP_FILE_PATH, "info_1", &info_1), info_1_path);

  assert_true(sPathExists(TMP_FILE_PATH)   == false);
  assert_true(sPathExists("tmp/some-file") == true);
  testSafeOverwriting(repoWriterOpenRaw("tmp", TMP_FILE_PATH, "some-file", "tmp/some-file"), "tmp/some-file");
  testGroupEnd();

  testGroupStart("behaviour with existing tmp-file");
  sRename(info_1_path, TMP_FILE_PATH);
  assert_true(sStat(TMP_FILE_PATH).st_size == 15);
  testWithExistingTmpFile(repoWriterOpenFile("tmp", TMP_FILE_PATH, "info_2", &info_2), info_2_path);

  sRename("tmp/some-file", TMP_FILE_PATH);
  assert_true(sStat(TMP_FILE_PATH).st_size == 15);
  testWithExistingTmpFile(repoWriterOpenRaw("tmp", TMP_FILE_PATH, "another-file", "tmp/another-file"), "tmp/another-file");
  testGroupEnd();

  testGroupStart("overwrite with empty file");
  repoWriterClose(repoWriterOpenFile("tmp", TMP_FILE_PATH, "info_2", &info_2));
  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sStat(info_2_path).st_size == 0);

  repoWriterClose(repoWriterOpenRaw("tmp", TMP_FILE_PATH, "another-file", "tmp/another-file"));
  assert_true(sPathExists(TMP_FILE_PATH)        == false);
  assert_true(sStat("tmp/another-file").st_size == 0);
  testGroupEnd();
}