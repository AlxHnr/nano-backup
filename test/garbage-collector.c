#include "garbage-collector.h"

#include "backup-dummy-hashes.h"
#include "metadata-util.h"
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
  testGroupStart("exclude internal files from deletion");
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

static Metadata *genTestMetadata(CR_Region *r)
{
  Metadata *metadata = createEmptyMetadata(r, 3);
  initHistPoint(metadata, 0, 0, 1234);
  initHistPoint(metadata, 1, 1, 7890);
  initHistPoint(metadata, 2, 2, 9876);

  PathNode *tmpdir = createPathNode("tmp", BPOL_none, NULL, metadata);
  appendHistDirectory(r, tmpdir, &metadata->backup_history[2], 12, 8, INT32_MAX, 0777);
  metadata->paths = tmpdir;

  appendHistRegular(r, createPathNode("foo.txt", BPOL_mirror, tmpdir, metadata), &metadata->backup_history[1], 91,
                    47, 680123, 0223, 144, some_file_hash, 0);

  PathNode *lost_file = createPathNode("unneeded.txt", BPOL_mirror, tmpdir, metadata);
  appendHistRegular(r, lost_file, &metadata->backup_history[0], 91, 47, 680123, 0223, 120, super_hash, 0);
  lost_file->hint = BH_not_part_of_repository;

  PathNode *subdir = createPathNode("subdir", BPOL_track, tmpdir, metadata);
  /* Subdir was a regular file in its previous backup state. */
  appendHistDirectory(r, subdir, &metadata->backup_history[0], 3, 5, 102934, 0123);
  appendHistNonExisting(r, subdir, &metadata->backup_history[1]);
  appendHistRegular(r, subdir, &metadata->backup_history[2], 91, 47, 680123, 0223, 191, three_hash, 0);

  /* Shares deduplicated content with "foo.txt". */
  appendHistRegular(r, createPathNode("bar.txt", BPOL_copy, subdir, metadata), &metadata->backup_history[0], 91,
                    47, 680123, 0223, 144, some_file_hash, 0);

  appendHistRegular(r, createPathNode("small.txt", BPOL_track, subdir, metadata), &metadata->backup_history[0], 91,
                    47, 680123, 0223, 17, (uint8_t *)"small inline data", 0);
  appendHistRegular(r, createPathNode("small2.txt", BPOL_track, subdir, metadata), &metadata->backup_history[0],
                    91, 47, 680123, 0223, 20, (uint8_t *)"small inline data 20", 0);

  PathNode *symlink = createPathNode("symlink.txt", BPOL_track, subdir, metadata);
  appendHistSymlink(r, symlink, &metadata->backup_history[0], 59, 23, "symlink content");
  symlink->history->state.metadata.file_info.size = 200;

  return metadata;
}

static void testWithComplexMetadata(CR_Region *r)
{
  testGroupStart("preserve files referenced by metadata");
  Metadata *metadata = genTestMetadata(r);
  StringView three_hash_path = str("tmp/repo/c/cf/44e30207cdd286c592fb4384aa9585598caabxbfx0");
  StringView some_file_hash_path = str("tmp/repo/7/f1/1e53c1ddfc806aa108f531847debf26ac9f5ex90x0");

  /* Create repo with referenced files. */
  sMkdir(str("tmp/repo"));
  sMkdir(str("tmp/repo/c"));
  sMkdir(str("tmp/repo/c/cf"));
  sFclose(sFopenWrite(three_hash_path));
  sMkdir(str("tmp/repo/7"));
  sMkdir(str("tmp/repo/7/f1"));
  sFclose(sFopenWrite(some_file_hash_path));

  /* Create excess files to be removed. */
  sMkdir(str("tmp/repo/e"));
  sMkdir(str("tmp/repo/7/f2"));
  sFclose(sFopenWrite(str("tmp/repo/e/foo.txt")));
  sFclose(sFopenWrite(str("tmp/repo/7/f1/bar.txt")));
  sFclose(sFopenWrite(str("tmp/repo/foobar.txt")));

  /* These files will falsely be preserved if the gc stringifies small files, directories or symlinks. */
  StringView small_inline_file = str("tmp/repo/7/36/d616c6c20696e6c696e652064617461000000x11x0");
  sMkdir(str("tmp/repo/7/36"));
  sFclose(sFopenWrite(small_inline_file));
  StringView small_inline_file_20 = str("tmp/repo/7/36/d616c6c20696e6c696e652064617461203230x14x0");
  sFclose(sFopenWrite(small_inline_file_20));
  StringView stringified_symlink = str("tmp/repo/0/00/0000000000000000000000000000000000000xc8x0");
  sMkdir(str("tmp/repo/0"));
  sMkdir(str("tmp/repo/0/00"));
  sFclose(sFopenWrite(stringified_symlink));
  StringView stringified_directory = str("tmp/repo/0/00/0000000000000000000000000000000000000x0x0");
  sFclose(sFopenWrite(stringified_directory));

  /* File marked as BH_not_part_of_repository. */
  sMkdir(str("tmp/repo/c/17"));
  StringView super_hash_path = str("tmp/repo/c/17/4c9dca0c3e380e14cbece6616f2c65f157b56x78x0");
  sFclose(sFopenWrite(super_hash_path));

  const GCStatistics stats = collectGarbage(metadata, str("tmp/repo"));
  assert_true(sPathExists(str("tmp/repo")));
  assert_true(sPathExists(str("tmp/repo/c")));
  assert_true(sPathExists(str("tmp/repo/c/cf")));
  assert_true(sPathExists(three_hash_path));
  assert_true(sPathExists(str("tmp/repo/7")));
  assert_true(sPathExists(str("tmp/repo/7/f1")));
  assert_true(sPathExists(some_file_hash_path));
  assert_true(!sPathExists(str("tmp/repo/e")));
  assert_true(!sPathExists(str("tmp/repo/7/f2")));
  assert_true(!sPathExists(str("tmp/repo/e/foo.txt")));
  assert_true(!sPathExists(str("tmp/repo/7/f1/bar.txt")));
  assert_true(!sPathExists(str("tmp/repo/foobar.txt")));
  assert_true(!sPathExists(str("tmp/repo/7/36")));
  assert_true(!sPathExists(small_inline_file));
  assert_true(!sPathExists(small_inline_file_20));
  assert_true(!sPathExists(str("tmp/repo/0")));
  assert_true(!sPathExists(str("tmp/repo/0/00")));
  assert_true(!sPathExists(stringified_symlink));
  assert_true(!sPathExists(stringified_directory));
  assert_true(!sPathExists(str("tmp/repo/c/17")));
  assert_true(!sPathExists(super_hash_path));
  assert_true(stats.deleted_items_count == 14);
  assert_true(stats.deleted_items_total_size == 0);

  sRemoveRecursively(str("tmp/repo"));
  testGroupEnd();
}

int main(void)
{
  CR_Region *r = CR_RegionNew();

  testWithEmptyMetadata(r);
  testWithComplexMetadata(r);
  testExcludeInternalFiles(r);
  testSymlinkToRepository(r);
  testInvalidRepositoryPath(r);

  CR_RegionRelease(r);
}
