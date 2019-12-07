/** @file
  Tests functions for removing unreferenced files from the repository.
*/

#include "garbage-collector.h"

#include "test.h"
#include "test-common.h"
#include "path-builder.h"
#include "safe-wrappers.h"

/** Generates various dummy files in "tmp/repo". */
static void populateRepoWithDummyFiles(void)
{
  sMkdir(strWrap("tmp/repo/a"));
  sMkdir(strWrap("tmp/repo/a/b"));
  sMkdir(strWrap("tmp/repo/a/c"));
  sMkdir(strWrap("tmp/repo/a/c/d"));
  sMkdir(strWrap("tmp/repo/a/1"));
  sMkdir(strWrap("tmp/repo/a/2"));
  sMkdir(strWrap("tmp/repo/a/3"));
  sMkdir(strWrap("tmp/repo/a/3/4"));
  sMkdir(strWrap("tmp/repo/a/3/5"));
  sMkdir(strWrap("tmp/repo/a/3/6"));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/b/foo")));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/c/bar")));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/c/d/backup")));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/3/nano")));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/3/5/this")));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/3/5/is")));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/3/5/a")));
  sFclose(sFopenWrite(strWrap("tmp/repo/a/3/5/test")));
  sSymlink(strWrap("../file.txt"),          strWrap("tmp/repo/file.txt"));
  sSymlink(strWrap("foo"),                  strWrap("tmp/repo/a/b/bar"));
  sSymlink(strWrap("bar"),                  strWrap("tmp/repo/a/c/q"));
  sSymlink(strWrap("../../../a"),           strWrap("tmp/repo/a/3/6/link-1"));
  sSymlink(strWrap("../../../../repo"),     strWrap("tmp/repo/a/3/6/link-2"));
  sSymlink(strWrap("../../../../file.txt"), strWrap("tmp/repo/a/3/6/link-3"));
  sSymlink(strWrap("non-existing"),         strWrap("tmp/repo/a/2/broken"));
}

/** Wrapper around collectGarbage() which checks the returned stats. */
static void testCollectGarbage(Metadata *metadata, const char *repo_path,
                               size_t count, uint64_t size)
{
  GCStats stats = collectGarbage(metadata, strWrap(repo_path));

  assert_true(stats.count == count);
  assert_true(stats.size == size);
}

int main(void)
{
  testGroupStart("symlink handling");
  sMkdir(strWrap("tmp/repo"));
  sFclose(sFopenWrite(strWrap("tmp/file.txt")));
  Metadata *empty_metadata = metadataNew();

  /* Repository is simple directory. */
  populateRepoWithDummyFiles();
  testCollectGarbage(empty_metadata, "tmp/repo", 25, 0);
  assert_true(countItemsInDir("tmp/repo") == 0);
  assert_true(sPathExists(strWrap("tmp/file.txt")));

  /* Repository is symlink to directory. */
  populateRepoWithDummyFiles();
  sSymlink(strWrap("repo"), strWrap("tmp/link-to-repo"));
  testCollectGarbage(empty_metadata, "tmp/link-to-repo", 25, 0);
  assert_true(countItemsInDir("tmp/repo") == 0);
  assert_true(sPathExists(strWrap("tmp/link-to-repo")));
  assert_true(sPathExists(strWrap("tmp/file.txt")));

  /* Repository is symlink to file. */
  sRemove(strWrap("tmp/link-to-repo"));
  sSymlink(strWrap("file.txt"), strWrap("tmp/link-to-repo"));
  populateRepoWithDummyFiles();
  testCollectGarbage(empty_metadata, "tmp/link-to-repo", 0, 0);
  assert_true(countItemsInDir("tmp/repo") == 25);
  assert_true(sPathExists(strWrap("tmp/link-to-repo")));
  assert_true(sPathExists(strWrap("tmp/file.txt")));

  /* Repository is broken symlink. */
  sRemove(strWrap("tmp/link-to-repo"));
  sSymlink(strWrap("non-existing"), strWrap("tmp/link-to-repo"));

  assert_error(collectGarbage(empty_metadata, strWrap("tmp/link-to-repo")),
               "failed to access \"tmp/link-to-repo\": No such file or directory");

  assert_true(countItemsInDir("tmp/repo") == 25);
  assert_true(sPathExists(strWrap("tmp/link-to-repo")));
  assert_true(sPathExists(strWrap("tmp/file.txt")));
  testGroupEnd();
}
