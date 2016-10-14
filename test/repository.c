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
  Tests various helper functions for handling repository.
*/

#include "repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "safe-wrappers.h"

#define TMP_FILE_PATH "tmp/tmp-file"

/** Tests repoRegularFileExists() by creating the specific file.

  @param file_path The path of the file which will be created.
  @param info Informations describing the file.
*/
static void testFileExists(const char *file_path,
                           const char *subdir_path,
                           const char *subsubdir_path,
                           const RegularFileInfo *info)
{
  String repo_path = str("tmp");

  assert_true(sPathExists(subdir_path) == false);
  assert_true(repoRegularFileExists(repo_path, info) == false);
  sMkdir(subdir_path);
  assert_true(repoRegularFileExists(repo_path, info) == false);
  sMkdir(subsubdir_path);
  assert_true(repoRegularFileExists(repo_path, info) == false);
  sFclose(sFopenWrite(file_path));
  assert_true(repoRegularFileExists(repo_path, info) == true);

  sRemove(file_path);
  assert_true(repoRegularFileExists(repo_path, info) == false);
  sRemove(subsubdir_path);
  assert_true(repoRegularFileExists(repo_path, info) == false);
  sRemove(subdir_path);
  assert_true(repoRegularFileExists(repo_path, info) == false);
  assert_true(sPathExists(subdir_path) == false);
}

/** Writes "Hello backup!". */
static void writeTestFile(RepoWriter *writer)
{
  repoWriterWrite("Hello",  5, writer);
  repoWriterWrite(" ",      1, writer);
  repoWriterWrite("backup", 6, writer);
  repoWriterWrite("!",      1, writer);
}

/** Asserts that the given file contains the string "Hello backup!". */
static void checkTestFile(const char *file_path)
{
  FileContent content = sGetFilesContent(file_path);
  assert_true(content.size == 13);
  assert_true(memcmp(content.content, "Hello backup!", 13) == 0);
  free(content.content);
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

/** Tests repoBuildRegularFilePath().

  @param path The path of the final file relative to the current directory.
  @param info The file info to pass to repoBuildRegularFilePath().
*/
static void testRegularFilePathBuilding(const char *path,
                                        RegularFileInfo *info)
{
  static Buffer *buffer = NULL;

  repoBuildRegularFilePath(&buffer, info);
  assert_true(strcmp(buffer->data, &path[4]) == 0);
}

int main(void)
{
  const char *info_1_path = "tmp/0/70/a0d101316191c1f2225282b2e3134373a3d40x8bx18";
  RegularFileInfo info_1 =
  {
    .size = 139, .slot = 24,
    .hash =
    {
      0x07, 0x0a, 0x0d, 0x10, 0x13, 0x16, 0x19, 0x1c, 0x1f, 0x22,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };

  const char *info_2_path = "tmp/2/15/14d1d49151941393d2d251109552931350d45x21e98xff";
  RegularFileInfo info_2 =
  {
    .size = 138904, .slot = 255,
    .hash =
    {
      0x21, 0x51, 0x4d, 0x1d, 0x49, 0x15, 0x19, 0x41, 0x39, 0x3d,
      0x2d, 0x25, 0x11, 0x09, 0x55, 0x29, 0x31, 0x35, 0x0d, 0x45,
    },
  };

  const char *info_3_path = "tmp/4/b5/f2b134f473b1f2757333f17531b23372f435bxffffffffffffffffx0";
  RegularFileInfo info_3 =
  {
    .size = 18446744073709551615UL, .slot = 0,
    .hash =
    {
      0x4b, 0x5f, 0x2b, 0x13, 0x4f, 0x47, 0x3b, 0x1f, 0x27, 0x57,
      0x33, 0x3f, 0x17, 0x53, 0x1b, 0x23, 0x37, 0x2f, 0x43, 0x5b,
    },
  };

  const char *info_4_path = "tmp/0/00/000000000112233445566778899aabbccddeex0x27";
  RegularFileInfo info_4 =
  {
    .size = 0, .slot = 39,
    .hash =
    {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
    },
  };

  const char *info_5_path = "tmp/0/fb/a0d101316191c1f2225282b2e3134373a3d40x46x0";
  RegularFileInfo info_5 =
  {
    .size = 70, .slot = 0,
    .hash =
    {
      0x0f, 0xba, 0x0d, 0x10, 0x13, 0x16, 0x19, 0x1c, 0x1f, 0x22,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };

  const char *info_6_path = "tmp/0/fb/d28fb2948efac8b2c25282b2e3134373a3d40x9fc4x11";
  RegularFileInfo info_6 =
  {
    .size = 40900, .slot = 17,
    .hash =
    {
      0x0f, 0xbd, 0x28, 0xfb, 0x29, 0x48, 0xef, 0xac, 0x8b, 0x2c,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };

  testGroupStart("repoRegularFileExists()");
  assert_true(repoRegularFileExists(str("non-existing-path"), &info_1) == false);
  testFileExists(info_1_path, "tmp/0", "tmp/0/70", &info_1);
  testFileExists(info_2_path, "tmp/2", "tmp/2/15", &info_2);
  testFileExists(info_3_path, "tmp/4", "tmp/4/b5", &info_3);
  testFileExists(info_4_path, "tmp/0", "tmp/0/00", &info_4);
  testGroupEnd();

  testGroupStart("repoBuildRegularFilePath()");
  testRegularFilePathBuilding(info_1_path, &info_1);
  testRegularFilePathBuilding(info_2_path, &info_2);
  testRegularFilePathBuilding(info_3_path, &info_3);
  testRegularFilePathBuilding(info_4_path, &info_4);
  testRegularFilePathBuilding(info_5_path, &info_5);
  testRegularFilePathBuilding(info_6_path, &info_6);
  testGroupEnd();

  testGroupStart("write regular files to repository");
  assert_error(repoWriterOpenFile("non-existing-directory", "non-existing-directory/tmp-file", "foo", &info_1),
               "failed to open \"non-existing-directory/tmp-file\" for writing: No such file or directory");
  assert_error(repoWriterOpenFile("example.txt", "example.txt/tmp-file", "foo", &info_2),
               "failed to open \"example.txt/tmp-file\" for writing: Not a directory");

  /* Write a new file without existing parent directories. */
  assert_true(sPathExists("tmp/0") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == false);

  RepoWriter *writer = repoWriterOpenFile("tmp", TMP_FILE_PATH, "info_1", &info_1);

  assert_true(writer != NULL);
  assert_true(sPathExists("tmp/0") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  writeTestFile(writer);

  assert_true(sPathExists("tmp/0") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  repoWriterClose(writer);

  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(info_1_path) == true);
  checkTestFile(info_1_path);

  /* Write a new file without the existing subdirectory. */
  assert_true(sPathExists("tmp/0") == true);
  assert_true(sPathExists("tmp/0/fb") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == false);

  writer = repoWriterOpenFile("tmp", TMP_FILE_PATH, "info_5", &info_5);

  assert_true(writer != NULL);
  assert_true(sPathExists("tmp/0") == true);
  assert_true(sPathExists("tmp/0/fb") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  writeTestFile(writer);

  assert_true(sPathExists("tmp/0") == true);
  assert_true(sPathExists("tmp/0/fb") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  repoWriterClose(writer);

  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(info_5_path) == true);
  checkTestFile(info_5_path);

  /* Write a new file with existing parent directories. */
  assert_true(sPathExists("tmp/0/fb") == true);
  assert_true(sPathExists(info_6_path) == false);
  assert_true(sPathExists(TMP_FILE_PATH) == false);

  writer = repoWriterOpenFile("tmp", TMP_FILE_PATH, "info_6", &info_6);

  assert_true(writer != NULL);
  assert_true(sPathExists("tmp/0/fb") == true);
  assert_true(sPathExists(info_6_path) == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  writeTestFile(writer);

  assert_true(sPathExists("tmp/0/fb") == true);
  assert_true(sPathExists(info_6_path) == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  repoWriterClose(writer);

  assert_true(sPathExists(TMP_FILE_PATH) == false);
  assert_true(sPathExists(info_6_path) == true);
  checkTestFile(info_6_path);
  testGroupEnd();

  testGroupStart("write to repository in raw mode");
  assert_error(repoWriterOpenRaw("non-existing-directory", "non-existing-directory/tmp-file", "foo", "tmp/foo"),
               "failed to open \"non-existing-directory/tmp-file\" for writing: No such file or directory");
  assert_error(repoWriterOpenRaw("example.txt", "example.txt/tmp-file", "bar", "tmp/bar"),
               "failed to open \"example.txt/tmp-file\" for writing: Not a directory");

  assert_true(sPathExists("some-file") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == false);

  writer = repoWriterOpenRaw("tmp", TMP_FILE_PATH, "some-file", "tmp/some-file");

  assert_true(writer != NULL);
  assert_true(sPathExists("tmp/some-file") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  writeTestFile(writer);

  assert_true(sPathExists("tmp/some-file") == false);
  assert_true(sPathExists(TMP_FILE_PATH) == true);

  repoWriterClose(writer);

  assert_true(sPathExists("tmp/some-file") == true);
  assert_true(sPathExists(TMP_FILE_PATH) == false);
  checkTestFile("tmp/some-file");
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
  assert_true(sPathExists("tmp/2") == false);
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

  testGroupStart("reading from repository");
  assert_error(repoReaderOpenFile("tmp", "info_1", &info_1),
               "failed to open \"info_1\" in \"tmp\": No such file or directory");
  assert_true(mkdir(info_1_path, 0) == 0);
  assert_error(repoReaderOpenFile("tmp", "info_1", &info_1),
               "failed to open \"info_1\" in \"tmp\": Permission denied");
  assert_true(rmdir(info_1_path) == 0);

  FileStream *stream = sFopenWrite(info_1_path);
  sFwrite("This is an example text.", 24, stream);
  sFclose(stream);

  RepoReader *reader = repoReaderOpenFile("tmp", "info_1", &info_1);
  assert_true(reader != NULL);

  char buffer[25] = { 0 };
  repoReaderRead(buffer, 14, reader);
  assert_true(strcmp(buffer, "This is an exa") == 0);

  memset(buffer, 0, sizeof(buffer));
  repoReaderRead(buffer, 10, reader);
  assert_true(strcmp(buffer, "mple text.") == 0);
  repoReaderClose(reader);

  reader = repoReaderOpenFile("tmp", "info_1", &info_1);
  assert_true(reader != NULL);

  assert_error(repoReaderRead(buffer, 25, reader),
               "reading \"info_1\" from \"tmp\": reached end of file unexpectedly");

  reader = repoReaderOpenFile("tmp", "info_1", &info_1);
  assert_true(reader != NULL);

  memset(buffer, 0, sizeof(buffer));
  repoReaderRead(buffer, 23, reader);
  assert_true(strcmp(buffer, "This is an example text") == 0);

  memset(buffer, 0, sizeof(buffer));
  repoReaderRead(buffer, 1, reader);
  assert_true(strcmp(buffer, ".") == 0);

  assert_error(repoReaderRead(buffer, 1, reader),
               "reading \"info_1\" from \"tmp\": reached end of file unexpectedly");
  testGroupEnd();
}
