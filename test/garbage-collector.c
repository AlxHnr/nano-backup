#include "garbage-collector.h"

#include "safe-wrappers.h"
#include "test-common.h"
#include "test.h"

static void populateRepoWithDummyFiles(void)
{
  sMkdir(str("tmp/repo"));
  sMkdir(str("tmp/repo/a"));
  sMkdir(str("tmp/repo/a/b"));
  sMkdir(str("tmp/repo/a/c"));
  sMkdir(str("tmp/repo/a/c/d"));
  sMkdir(str("tmp/repo/a/1"));
  sMkdir(str("tmp/repo/a/2"));
  sMkdir(str("tmp/repo/a/3"));
  sMkdir(str("tmp/repo/a/3/4"));
  sMkdir(str("tmp/repo/a/3/5"));
  sMkdir(str("tmp/repo/a/3/6"));
  sFclose(sFopenWrite(str("tmp/repo/a/b/foo")));
  sFclose(sFopenWrite(str("tmp/repo/a/c/bar")));
  sFclose(sFopenWrite(str("tmp/repo/a/c/d/backup")));
  sFclose(sFopenWrite(str("tmp/repo/a/3/nano")));
  sFclose(sFopenWrite(str("tmp/repo/a/3/5/this")));
  sFclose(sFopenWrite(str("tmp/repo/a/3/5/is")));
  sFclose(sFopenWrite(str("tmp/repo/a/3/5/a")));
  sFclose(sFopenWrite(str("tmp/repo/a/3/5/test")));
  sSymlink(str("../file.txt"), str("tmp/repo/file.txt"));
  sSymlink(str("foo"), str("tmp/repo/a/b/bar"));
  sSymlink(str("bar"), str("tmp/repo/a/c/q"));
  sSymlink(str("../../../a"), str("tmp/repo/a/3/6/link-1"));
  sSymlink(str("../../../../repo"), str("tmp/repo/a/3/6/link-2"));
  sSymlink(str("../../../../file.txt"), str("tmp/repo/a/3/6/link-3"));
  sSymlink(str("non-existing"), str("tmp/repo/a/2/broken"));
}

static void testCollectGarbage(const Metadata *metadata, const char *repo_path, const size_t count,
                               const uint64_t size)
{
  const GCStatistics stats = collectGarbage(metadata, str(repo_path));

  assert_true(stats.deleted_items_count == count);
  assert_true(stats.deleted_items_total_size == size);
}

static void testWithEmptyMetadata(CR_Region *r)
{
  testGroupStart("delete unreferenced files");
  sFclose(sFopenWrite(str("tmp/file.txt")));

  populateRepoWithDummyFiles();
  testCollectGarbage(metadataNew(r), "tmp/repo", 25, 0);
  assert_true(countItemsInDir("tmp/repo") == 0);
  assert_true(sPathExists(str("tmp/file.txt")));
  sRemoveRecursively(str("tmp/repo"));
  testGroupEnd();
}

static void testSymlinkToRepository(CR_Region *r)
{
  testGroupStart("repository is symlink to directory");
  populateRepoWithDummyFiles();
  sSymlink(str("repo"), str("tmp/link-to-repo"));
  testCollectGarbage(metadataNew(r), "tmp/link-to-repo", 25, 0);
  assert_true(countItemsInDir("tmp/repo") == 0);
  assert_true(sPathExists(str("tmp/link-to-repo")));
  assert_true(sPathExists(str("tmp/file.txt")));
  sRemoveRecursively(str("tmp/repo"));
  testGroupEnd();
}

static void testInvalidRepositoryPath(CR_Region *r)
{
  testGroupStart("invalid symlink to repository");
  Metadata *metadata = metadataNew(r);
  populateRepoWithDummyFiles();

  /* Repository is symlink to file. */
  sRemove(str("tmp/link-to-repo"));
  sSymlink(str("file.txt"), str("tmp/link-to-repo"));
  assert_error_errno(collectGarbage(metadata, str("tmp/link-to-repo")),
                     "failed to open directory \"tmp/link-to-repo\"", ENOTDIR);

  /* Repository is broken symlink. */
  sRemove(str("tmp/link-to-repo"));
  sSymlink(str("non-existing"), str("tmp/link-to-repo"));
  assert_error_errno(collectGarbage(metadata, str("tmp/link-to-repo")),
                     "failed to open directory \"tmp/link-to-repo\"", ENOENT);

  assert_true(countItemsInDir("tmp/repo") == 25);
  assert_true(sPathExists(str("tmp/link-to-repo")));
  assert_true(sPathExists(str("tmp/file.txt")));

  sRemoveRecursively(str("tmp/repo"));
  testGroupEnd();
}

static void testExcludeInternalFiles(CR_Region *r)
{
  testGroupStart("excluding internal files from deletion");
  sMkdir(str("tmp/repo"));
  sFclose(sFopenWrite(str("tmp/repo/config")));
  sFclose(sFopenWrite(str("tmp/repo/metadata")));
  sFclose(sFopenWrite(str("tmp/repo/lockfile")));
  testCollectGarbage(metadataNew(r), "tmp/repo", 0, 0);
  assert_true(sPathExists(str("tmp/repo/config")));
  assert_true(sPathExists(str("tmp/repo/metadata")));
  assert_true(sPathExists(str("tmp/repo/lockfile")));
  sRemoveRecursively(str("tmp/repo"));
  testGroupEnd();
}

int main(void)
{
  CR_Region *r = CR_RegionNew();

  testWithEmptyMetadata(r);
  testSymlinkToRepository(r);
  testInvalidRepositoryPath(r);
  testExcludeInternalFiles(r);

  CR_RegionRelease(r);
}
