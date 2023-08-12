#include "repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "error-handling.h"
#include "safe-wrappers.h"
#include "test.h"

#define TMP_FILE_PATH strWrap("tmp/tmp-file")

/** Tests repoRegularFileExists() by creating the specific file.

  @param file_path The path of the file which will be created.
  @param info Informations describing the file.
*/
static void testFileExists(String file_path, String subdir_path, String subsubdir_path,
                           const RegularFileInfo *info)
{
  String repo_path = strWrap("tmp");

  assert_true(!sPathExists(subdir_path));
  assert_true(!repoRegularFileExists(repo_path, info));
  sMkdir(subdir_path);
  assert_true(!repoRegularFileExists(repo_path, info));
  sMkdir(subsubdir_path);
  assert_true(!repoRegularFileExists(repo_path, info));
  sFclose(sFopenWrite(file_path));
  assert_true(repoRegularFileExists(repo_path, info));

  sRemove(file_path);
  assert_true(!repoRegularFileExists(repo_path, info));
  sRemove(subsubdir_path);
  assert_true(!repoRegularFileExists(repo_path, info));
  sRemove(subdir_path);
  assert_true(!repoRegularFileExists(repo_path, info));
  assert_true(!sPathExists(subdir_path));
}

static void writeTestFile(RepoWriter *writer)
{
  repoWriterWrite("Hello", 5, writer);
  repoWriterWrite(" ", 1, writer);
  repoWriterWrite("backup", 6, writer);
  repoWriterWrite("!", 1, writer);
}

static void checkFilesContent(String file_path, const char *expected_content)
{
  const size_t expected_size = strlen(expected_content);

  CR_Region *r = CR_RegionNew();
  const FileContent content = sGetFilesContent(r, file_path);

  if(content.size != expected_size)
  {
    die("content size: %zu != %zu: \"%s\"", content.size, expected_size, file_path.content);
  }
  else if(memcmp(content.content, expected_content, expected_size) != 0)
  {
    die("file has invalid content: \"%s\"", file_path.content);
  }

  CR_RegionRelease(r);
}

static void checkTestFile(String file_path)
{
  checkFilesContent(file_path, "Hello backup!");
}

/** Overwrites the given filepath with the file represented by the
  RepoWriter.

  @param writer A new, unused writer used for overwriting the final
  filepath. It will be destroyed by this function.
  @param final_path The file which should be overwritten by this test.
*/
static void testSafeOverwriting(RepoWriter *writer, String final_path)
{
  assert_true(sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(final_path));
  assert_true(writer != NULL);
  checkTestFile(final_path);

  repoWriterWrite("This", 4, writer);
  repoWriterWrite(" is", 3, writer);
  repoWriterWrite(" a ", 3, writer);
  repoWriterWrite("test.", 5, writer);
  checkTestFile(final_path);

  assert_true(sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(final_path));
  repoWriterClose(writer);
  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(final_path));

  checkFilesContent(final_path, "This is a test.");
}

/** Tests the given RepoWriter. The repositories temporary file must have
  existed before the writer was opened.

  @param writer A new, unused RepoWriter. It will be destroyed by this
  function.
  @param final_path The file which should be created by finalizing the
  given RepoWriter.
*/
static void testWithExistingTmpFile(RepoWriter *writer, String final_path)
{
  assert_true(sPathExists(TMP_FILE_PATH));
  assert_true(!sPathExists(final_path));

  repoWriterWrite("Nano Backup", 11, writer);
  repoWriterClose(writer);

  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(final_path));

  checkFilesContent(final_path, "Nano Backup");
}

/** Tests repoBuildRegularFilePath().

  @param path The path of the final file relative to the current directory.
  @param info The file info to pass to repoBuildRegularFilePath().
*/
static void testRegularFilePathBuilding(String path, const RegularFileInfo *info)
{
  static char *buffer = NULL;

  repoBuildRegularFilePath(&buffer, info);
  assert_true(strcmp(buffer, &path.content[4]) == 0);
}

int main(void)
{
  String info_1_path = strWrap("tmp/0/70/a0d101316191c1f2225282b2e3134373a3d40x8bx18");
  const RegularFileInfo info_1 =
  {
    .size = 139, .slot = 24,
    .hash =
    {
      0x07, 0x0a, 0x0d, 0x10, 0x13, 0x16, 0x19, 0x1c, 0x1f, 0x22,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };

  String info_2_path = strWrap("tmp/2/15/14d1d49151941393d2d251109552931350d45x21e98xff");
  const RegularFileInfo info_2 =
  {
    .size = 138904, .slot = 255,
    .hash =
    {
      0x21, 0x51, 0x4d, 0x1d, 0x49, 0x15, 0x19, 0x41, 0x39, 0x3d,
      0x2d, 0x25, 0x11, 0x09, 0x55, 0x29, 0x31, 0x35, 0x0d, 0x45,
    },
  };

  String info_3_path = strWrap("tmp/4/b5/f2b134f473b1f2757333f17531b23372f435bxffffffffffffffffx0");
  const RegularFileInfo info_3 =
  {
    .size = 18446744073709551615UL, .slot = 0,
    .hash =
    {
      0x4b, 0x5f, 0x2b, 0x13, 0x4f, 0x47, 0x3b, 0x1f, 0x27, 0x57,
      0x33, 0x3f, 0x17, 0x53, 0x1b, 0x23, 0x37, 0x2f, 0x43, 0x5b,
    },
  };

  String info_4_path = strWrap("tmp/0/00/000000000112233445566778899aabbccddeex0x27");
  const RegularFileInfo info_4 =
  {
    .size = 0, .slot = 39,
    .hash =
    {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
    },
  };

  String info_5_path = strWrap("tmp/0/fb/a0d101316191c1f2225282b2e3134373a3d40x46x0");
  const RegularFileInfo info_5 =
  {
    .size = 70, .slot = 0,
    .hash =
    {
      0x0f, 0xba, 0x0d, 0x10, 0x13, 0x16, 0x19, 0x1c, 0x1f, 0x22,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };

  String info_6_path = strWrap("tmp/0/fb/d28fb2948efac8b2c25282b2e3134373a3d40x9fc4x11");
  const RegularFileInfo info_6 =
  {
    .size = 40900, .slot = 17,
    .hash =
    {
      0x0f, 0xbd, 0x28, 0xfb, 0x29, 0x48, 0xef, 0xac, 0x8b, 0x2c,
      0x25, 0x28, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3d, 0x40,
    },
  };

  testGroupStart("repoRegularFileExists()");
  assert_true(!repoRegularFileExists(strWrap("non-existing-path"), &info_1));
  testFileExists(info_1_path, strWrap("tmp/0"), strWrap("tmp/0/70"), &info_1);
  testFileExists(info_2_path, strWrap("tmp/2"), strWrap("tmp/2/15"), &info_2);
  testFileExists(info_3_path, strWrap("tmp/4"), strWrap("tmp/4/b5"), &info_3);
  testFileExists(info_4_path, strWrap("tmp/0"), strWrap("tmp/0/00"), &info_4);
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
  assert_error_errno(repoWriterOpenFile(strWrap("non-existing-directory"),
                                        strWrap("non-existing-directory/tmp-file"), strWrap("foo"), &info_1),
                     "failed to open \"non-existing-directory/tmp-file\" for writing", ENOENT);
  assert_error_errno(
    repoWriterOpenFile(strWrap("example.txt"), strWrap("example.txt/tmp-file"), strWrap("foo"), &info_2),
    "failed to open \"example.txt/tmp-file\" for writing", ENOTDIR);

  /* Write a new file without existing parent directories. */
  assert_true(!sPathExists(strWrap("tmp/0")));
  assert_true(!sPathExists(TMP_FILE_PATH));

  RepoWriter *writer = repoWriterOpenFile(strWrap("tmp"), TMP_FILE_PATH, strWrap("info_1"), &info_1);

  assert_true(writer != NULL);
  assert_true(!sPathExists(strWrap("tmp/0")));
  assert_true(sPathExists(TMP_FILE_PATH));

  writeTestFile(writer);

  assert_true(!sPathExists(strWrap("tmp/0")));
  assert_true(sPathExists(TMP_FILE_PATH));

  repoWriterClose(writer);

  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(info_1_path));
  checkTestFile(info_1_path);

  /* Write a new file without the existing subdirectory. */
  assert_true(sPathExists(strWrap("tmp/0")));
  assert_true(!sPathExists(strWrap("tmp/0/fb")));
  assert_true(!sPathExists(TMP_FILE_PATH));

  writer = repoWriterOpenFile(strWrap("tmp"), TMP_FILE_PATH, strWrap("info_5"), &info_5);

  assert_true(writer != NULL);
  assert_true(sPathExists(strWrap("tmp/0")));
  assert_true(!sPathExists(strWrap("tmp/0/fb")));
  assert_true(sPathExists(TMP_FILE_PATH));

  writeTestFile(writer);

  assert_true(sPathExists(strWrap("tmp/0")));
  assert_true(!sPathExists(strWrap("tmp/0/fb")));
  assert_true(sPathExists(TMP_FILE_PATH));

  repoWriterClose(writer);

  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(info_5_path));
  checkTestFile(info_5_path);

  /* Write a new file with existing parent directories. */
  assert_true(sPathExists(strWrap("tmp/0/fb")));
  assert_true(!sPathExists(info_6_path));
  assert_true(!sPathExists(TMP_FILE_PATH));

  writer = repoWriterOpenFile(strWrap("tmp"), TMP_FILE_PATH, strWrap("info_6"), &info_6);

  assert_true(writer != NULL);
  assert_true(sPathExists(strWrap("tmp/0/fb")));
  assert_true(!sPathExists(info_6_path));
  assert_true(sPathExists(TMP_FILE_PATH));

  writeTestFile(writer);

  assert_true(sPathExists(strWrap("tmp/0/fb")));
  assert_true(!sPathExists(info_6_path));
  assert_true(sPathExists(TMP_FILE_PATH));

  repoWriterClose(writer);

  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(info_6_path));
  checkTestFile(info_6_path);
  testGroupEnd();

  testGroupStart("write to repository in raw mode");
  assert_error_errno(repoWriterOpenRaw(strWrap("non-existing-directory"),
                                       strWrap("non-existing-directory/tmp-file"), strWrap("foo"),
                                       strWrap("tmp/foo")),
                     "failed to open \"non-existing-directory/tmp-file\" for writing", ENOENT);
  assert_error_errno(
    repoWriterOpenRaw(strWrap("example.txt"), strWrap("example.txt/tmp-file"), strWrap("bar"), strWrap("tmp/bar")),
    "failed to open \"example.txt/tmp-file\" for writing", ENOTDIR);

  assert_true(!sPathExists(strWrap("some-file")));
  assert_true(!sPathExists(TMP_FILE_PATH));

  writer = repoWriterOpenRaw(strWrap("tmp"), TMP_FILE_PATH, strWrap("some-file"), strWrap("tmp/some-file"));

  assert_true(writer != NULL);
  assert_true(!sPathExists(strWrap("tmp/some-file")));
  assert_true(sPathExists(TMP_FILE_PATH));

  writeTestFile(writer);

  assert_true(!sPathExists(strWrap("tmp/some-file")));
  assert_true(sPathExists(TMP_FILE_PATH));

  repoWriterClose(writer);

  assert_true(sPathExists(strWrap("tmp/some-file")));
  assert_true(!sPathExists(TMP_FILE_PATH));
  checkTestFile(strWrap("tmp/some-file"));
  testGroupEnd();

  testGroupStart("safe overwriting");
  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(info_1_path));
  testSafeOverwriting(repoWriterOpenFile(strWrap("tmp"), TMP_FILE_PATH, strWrap("info_1"), &info_1), info_1_path);

  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sPathExists(strWrap("tmp/some-file")));
  testSafeOverwriting(
    repoWriterOpenRaw(strWrap("tmp"), TMP_FILE_PATH, strWrap("some-file"), strWrap("tmp/some-file")),
    strWrap("tmp/some-file"));
  testGroupEnd();

  testGroupStart("behaviour with existing tmp-file");
  sRename(info_1_path, TMP_FILE_PATH);
  assert_true(sStat(TMP_FILE_PATH).st_size == 15);
  assert_true(!sPathExists(strWrap("tmp/2")));
  testWithExistingTmpFile(repoWriterOpenFile(strWrap("tmp"), TMP_FILE_PATH, strWrap("info_2"), &info_2),
                          info_2_path);

  sRename(strWrap("tmp/some-file"), TMP_FILE_PATH);
  assert_true(sStat(TMP_FILE_PATH).st_size == 15);
  testWithExistingTmpFile(
    repoWriterOpenRaw(strWrap("tmp"), TMP_FILE_PATH, strWrap("another-file"), strWrap("tmp/another-file")),
    strWrap("tmp/another-file"));
  testGroupEnd();

  testGroupStart("overwriting tmp-file with itself");
  FileStream *stream = sFopenWrite(TMP_FILE_PATH);
  sFwrite("-include build/dependencies.makefile\n", 37, stream);
  sFclose(stream);

  checkFilesContent(TMP_FILE_PATH, "-include build/dependencies.makefile\n");

  writer = repoWriterOpenRaw(strWrap("tmp"), TMP_FILE_PATH, TMP_FILE_PATH, TMP_FILE_PATH);
  assert_true(writer != NULL);
  repoWriterWrite("nano-backup backups files", 25, writer);
  repoWriterClose(writer);

  checkFilesContent(TMP_FILE_PATH, "nano-backup backups files");

  writer = repoWriterOpenRaw(strWrap("tmp"), TMP_FILE_PATH, TMP_FILE_PATH, TMP_FILE_PATH);
  assert_true(writer != NULL);
  repoWriterWrite("FOO BAR 321", 11, writer);
  repoWriterClose(writer);

  checkFilesContent(TMP_FILE_PATH, "FOO BAR 321");
  testGroupEnd();

  testGroupStart("overwrite with empty file");
  repoWriterClose(repoWriterOpenFile(strWrap("tmp"), TMP_FILE_PATH, strWrap("info_2"), &info_2));
  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sStat(info_2_path).st_size == 0);

  repoWriterClose(
    repoWriterOpenRaw(strWrap("tmp"), TMP_FILE_PATH, strWrap("another-file"), strWrap("tmp/another-file")));
  assert_true(!sPathExists(TMP_FILE_PATH));
  assert_true(sStat(strWrap("tmp/another-file")).st_size == 0);
  testGroupEnd();

  testGroupStart("reading from repository");
  assert_error_errno(repoReaderOpenFile(strWrap("tmp"), strWrap("info_1"), &info_1),
                     "failed to open \"info_1\" in \"tmp\"", ENOENT);
  assert_true(mkdir(info_1_path.content, 0) == 0);
  assert_error_errno(repoReaderOpenFile(strWrap("tmp"), strWrap("info_1"), &info_1),
                     "failed to open \"info_1\" in \"tmp\"", EACCES);
  assert_true(rmdir(info_1_path.content) == 0);

  stream = sFopenWrite(info_1_path);
  sFwrite("This is an example text.", 24, stream);
  sFclose(stream);

  RepoReader *reader = repoReaderOpenFile(strWrap("tmp"), strWrap("info_1"), &info_1);
  assert_true(reader != NULL);

  char buffer[25] = { 0 };
  repoReaderRead(buffer, 14, reader);
  assert_true(strcmp(buffer, "This is an exa") == 0);

  memset(buffer, 0, sizeof(buffer));
  repoReaderRead(buffer, 10, reader);
  assert_true(strcmp(buffer, "mple text.") == 0);
  repoReaderClose(reader);

  reader = repoReaderOpenFile(strWrap("tmp"), strWrap("info_1"), &info_1);
  assert_true(reader != NULL);

  assert_error(repoReaderRead(buffer, 25, reader),
               "reading \"info_1\" from \"tmp\": reached end of file unexpectedly");

  reader = repoReaderOpenFile(strWrap("tmp"), strWrap("info_1"), &info_1);
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

  testGroupStart("Locking repository");
  assert_error_errno(repoLockUntilExit(strWrap("tmp/non/existing/path")),
                     "failed to create lockfile: \"tmp/non/existing/path/lockfile\"", ENOENT);

  repoLockUntilExit(strWrap("tmp"));
  assert_true(sPathExists(strWrap("tmp/lockfile")));
  testGroupEnd();
}
