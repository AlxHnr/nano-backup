#include "integrity.h"

#include "CRegion/region.h"
#include "backup.h"
#include "safe-wrappers.h"
#include "search-tree.h"
#include "string-table.h"
#include "test-common.h"
#include "test.h"

static StringView repo_path = { .content = "tmp/repo", .length = 8, .is_terminated = true };
static StringView metadata_path = { .content = "tmp/repo/metadata", .length = 17, .is_terminated = true };
static StringView tmp_file_path = { .content = "tmp/repo/tmp-file", .length = 17, .is_terminated = true };

static void writeToFile(const char *path, const char *content)
{
  FileStream *writer = sFopenWrite(str(path));
  sFwrite(content, strlen(content), writer);
  sFclose(writer);
}

static void makeBackup(CR_Region *r, Metadata *metadata)
{
  SearchNode *search_tree = searchTreeLoad(r, str("generated-config-files/integrity-test.txt"));
  initiateBackup(metadata, search_tree);
  finishBackup(metadata, repo_path, tmp_file_path);
  metadataWrite(metadata, repo_path, tmp_file_path, metadata_path);
}

int main(void)
{
  CR_Region *r = CR_RegionNew();

  testGroupStart("prepare backup repository");
  sMkdir(repo_path);
  sMkdir(str("tmp/files"));
  writeToFile("tmp/files/empty-file.txt", "");
  writeToFile("tmp/files/Another File.txt", "content of another file");
  writeToFile("tmp/files/extra-file.txt", "this is just an extra file created for testing");
  writeToFile("tmp/files/small file", "less than 20 bytes");
  writeToFile("tmp/files/smaller file", "1234");
  writeToFile("tmp/files/unchanged extra file", "this file gets created once and then never changes");
  writeToFile("tmp/files/20-bytes.txt", "20 byte large file!!");
  writeToFile("tmp/files/21-bytes.txt", "21 byte large file!!!");
  writeToFile("tmp/files/extra-file-for-deduplication.txt", "a b c d e f g h i j 01213131231");
  makeBackup(r, metadataNew(r));

  writeToFile("tmp/files/Another File.txt", "a b c d e f g h i j 01213131231");
  writeToFile("tmp/files/smaller file", "string slightly larger than 20 bytes");
  makeBackup(r, metadataLoad(r, metadata_path));
  makeBackup(r, metadataLoad(r, metadata_path)); /* Extra backup to enlarge history. */

  writeToFile("tmp/files/empty-file.txt", "xyz test test test test test 1234567890");
  writeToFile("tmp/files/Another File.txt", "");
  sRemove(str("tmp/files/smaller file"));
  writeToFile("tmp/files/newly-created-file.txt", "This is some test content of a new file.");
  writeToFile("tmp/files/additional-file-01", "a b c d e f g h i j 01213131231");
  writeToFile("tmp/files/additional-file-02", "This is some test content of a new file.");
  writeToFile("tmp/files/additional-file-03", "nano-backup nano-backup nano-backup");
  writeToFile("tmp/files/breaks-via-deduplication.txt", "content of another file");
  makeBackup(r, metadataLoad(r, metadata_path));

  writeToFile("tmp/files/Another File.txt", "content of another file");
  writeToFile("tmp/files/smaller file", "1234");
  makeBackup(r, metadataLoad(r, metadata_path));

  StringView cwd = getCwd(allocatorWrapRegion(r));
  const Metadata *metadata = metadataLoad(r, metadata_path);
  testGroupEnd();

  testGroupStart("checkIntegrity() on healthy repository");
  assert_true(checkIntegrity(r, metadata, repo_path) == NULL);
  testGroupEnd();

  testGroupStart("checkIntegrity() on corrupted repository");
  /* tmp/files/21-bytes.txt: overwrite content with same size. */
  writeToFile("tmp/repo/9/14/63ea1831fa59be6f547140553e6134f3ec0bbx15x0", "modified content here");
  /* tmp/files/unchanged extra file: overwrite content with different size. */
  writeToFile("tmp/repo/d/b2/4bcdd36e05535b459499592289600e8baf013x32x0", "content with different size here");
  /* tmp/files/empty-file.txt: delete history state. */
  sRemove(str("tmp/repo/8/d1/1e56f239ac968dfa0f587bb357cde360c7137x27x0"));
  /* tmp/files/smaller file: modify history state. */
  writeToFile("tmp/repo/3/62/c96d3be9b03223ed9507e4fabee4a424bc7bbx24x0", "string modified and is the same size");
  /* tmp/files/Another File.txt: modify deduplicated history state. */
  writeToFile("tmp/repo/3/9a/fc73eccf34f7cf5ff3fd564910f294610bdb3x17x0", "broken content 123412341234");
  /* tmp/files/additional-file-03: replace with non-file (symlink with same st_size). */
  sRemove(str("tmp/repo/8/4b/6afb97314b5c2f7b8eefede7f7f9c1db0c84fx23x0"));
  sSymlink(str("nano-backup nano-backup nano-backup"),
           str("tmp/repo/8/4b/6afb97314b5c2f7b8eefede7f7f9c1db0c84fx23x0"));

  size_t broken_path_node_count = 0;
  StringTable *broken_path_nodes = strTableNew(r);
  for(const ListOfBrokenPathNodes *path_node = checkIntegrity(r, metadata, repo_path); path_node != NULL;
      path_node = path_node->next)
  {
    assert_true(path_node->node->path.is_terminated);
    assert_true(strIsParentPath(cwd, path_node->node->path));
    StringView unique_subpath = strUnterminated(&path_node->node->path.content[cwd.length + 1],
                                                path_node->node->path.length - cwd.length - 1);
    assert_true(strTableGet(broken_path_nodes, unique_subpath) == NULL);
    strTableMap(broken_path_nodes, unique_subpath, (void *)0x1);
    broken_path_node_count++;
  }
  assert_true(strTableGet(broken_path_nodes, str("tmp/files/empty-file.txt")) != NULL);
  assert_true(strTableGet(broken_path_nodes, str("tmp/files/Another File.txt")) != NULL);
  assert_true(strTableGet(broken_path_nodes, str("tmp/files/smaller file")) != NULL);
  assert_true(strTableGet(broken_path_nodes, str("tmp/files/unchanged extra file")) != NULL);
  assert_true(strTableGet(broken_path_nodes, str("tmp/files/21-bytes.txt")) != NULL);
  assert_true(strTableGet(broken_path_nodes, str("tmp/files/additional-file-03")) != NULL);
  assert_true(strTableGet(broken_path_nodes, str("tmp/files/breaks-via-deduplication.txt")) != NULL);
  assert_true(broken_path_node_count == 7);
  testGroupEnd();

  CR_RegionRelease(r);
}
