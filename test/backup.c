/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

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
  Tests the core backup logic.
*/

#include "backup.h"

#include "test.h"
#include "metadata.h"
#include "search-tree.h"
#include "test-common.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** Finds the node that represents the directory in which this test runs.
  It will terminate the program if the node doesn't exist, or its parent
  nodes are invalid.

  @param metadata The metadata containing the nodes. It must be a valid
  metadata structure, so make sure to pass it to checkMetadata() first.
  @param cwd The current working directory.

  @return The found node.
*/
static PathNode *findCwdNode(Metadata *metadata, String cwd)
{
  for(PathNode *node = metadata->paths; node != NULL; node = node->subnodes)
  {
    if(node->policy != BPOL_none)
    {
      die("path shouldn't have a policy: \"%s\"", node->path.str);
    }
    else if(node->history->next != NULL)
    {
      die("path has too many history points: \"%s\"", node->path.str);
    }
    else if(node->next != NULL)
    {
      die("item is not the last in list: \"%s\"", node->path.str);
    }
    else if(node->history->state.type != PST_directory)
    {
      die("not a directory: \"%s\"", node->path.str);
    }
    else if(strCompare(node->path, cwd))
    {
      return node;
    }
  }

  die("path does not exist in metadata: \"%s\"", cwd.str);
  return NULL;
}

/** Simplified wrapper around findPathNode().

  @param node The node containing the requested subnode.
  @param subnode_name The name of the requested subnode. This should not be
  a full path.
  @param policy The policy of the requested subnode.
  @param requested_history_length The history length of the requested
  subnode.
  @param requested_subnode_count The amount of subnodes in the requested
  subnode.

  @return The requested subnode. If it doesn't exist, the program will be
  terminated with failure.
*/
static PathNode *findSubnode(PathNode *node,
                             const char *subnode_name,
                             BackupHint hint, BackupPolicy policy,
                             size_t requested_history_length,
                             size_t requested_subnode_count)
{
  String subnode_path = strAppendPath(node->path, str(subnode_name));
  return findPathNode(node->subnodes, subnode_path.str, hint, policy,
                      requested_history_length, requested_subnode_count);
}

/** Counts the path elements in the given string. E.g. "/home/foo/bar" has
  3 path elements.

  @param string The string containing the paths.

  @return The path element count.
*/
static size_t countPathElements(String string)
{
  size_t count = 0;

  for(size_t index = 0; index < string.length; index++)
  {
    count += string.str[index] == '/';
  }

  return count;
}

/** Counts the items in the specified directory, excluding "." and "..".

  @param path The path to a valid directory.

  @return The amount of files in the specified directory.
*/
static size_t countFilesInDir(const char *path)
{
  size_t counter = 0;
  DIR *dir = sOpenDir(path);

  while(sReadDir(dir, path) != NULL)
  {
    counter++;
  }

  sCloseDir(dir, path);
  return counter;
}

/** Simplified wrapper around mustHaveRegular(). It extracts additional
  informations via stat() and passes them to mustHaveRegular().

  @param node The node which should be checked.
  @param backup The backup to which the given node must point.
  @param hash The expected hash in RegularFileInfo.
  @param slot The expected slot in RegularFileInfo.
*/
static void mustHaveRegularStat(PathNode *node, Backup *backup,
                                uint8_t *hash, uint8_t slot)
{
  struct stat stats = sStat(node->path.str);
  mustHaveRegular(node, backup, stats.st_uid, stats.st_gid,
                  stats.st_mtime, stats.st_mode, stats.st_size,
                  hash, slot);
}

/** Simplified wrapper around mustHaveSymlink(). It extracts additional
  informations via lstat() and passes them to mustHaveSymlink().

  @param node The node which should be checked.
  @param backup The backup to which the given node must point.
  @param sym_target The target path of the symlink.
*/
static void mustHaveSymlinkLStat(PathNode *node, Backup *backup,
                                 const char *sym_target)
{
  struct stat stats = sLStat(node->path.str);
  mustHaveSymlink(node, backup, stats.st_uid, stats.st_gid,
                  stats.st_mtime, sym_target);
}

/** Like mustHaveRegularStat(), but for mustHaveDirectory(). */
static void mustHaveDirectoryStat(PathNode *node, Backup *backup)
{
  struct stat stats = sStat(node->path.str);
  mustHaveDirectory(node, backup, stats.st_uid, stats.st_gid,
                    stats.st_mtime, stats.st_mode);
}

/** Checks a metadata tree containing files found using
  "generated-config-files/backup-search-test.txt".

  @param metadata A valid metadata tree.
  @param cwd The path to the current working directory.
  @param cwd_depth The amount of elements in the given cwd.
*/
static void checkBackupSearchData(Metadata *metadata, String cwd,
                                  size_t cwd_depth)
{
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 10);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 10);

  PathNode *data_dir = findCwdNode(metadata, cwd);
  assert_true(data_dir->subnodes != NULL);
  assert_true(data_dir->subnodes->next == NULL);

  PathNode *test_dir = findSubnode(data_dir, "test directory", BH_added, BPOL_none, 1, 5);
  mustHaveDirectoryStat(test_dir, &metadata->current_backup);

  PathNode *bar_a_txt = findSubnode(test_dir, "bar-a.txt", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(bar_a_txt, &metadata->current_backup, NULL, 0);

  PathNode *foobar_a1_txt = findSubnode(test_dir, "foobar a1.txt", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(foobar_a1_txt, &metadata->current_backup, NULL, 0);

  PathNode *empty_dir = findSubnode(test_dir, "empty-directory", BH_added, BPOL_mirror, 1, 0);
  mustHaveSymlinkLStat(empty_dir, &metadata->current_backup, ".empty");

  PathNode *euro_txt = findSubnode(test_dir, "€.txt", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(euro_txt, &metadata->current_backup, NULL, 0);

  PathNode *foo_1 = findSubnode(test_dir, "foo 1", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(foo_1, &metadata->current_backup);

  PathNode *bar = findSubnode(foo_1, "bar", BH_added, BPOL_track, 1, 3);
  mustHaveDirectoryStat(bar, &metadata->current_backup);

  PathNode *one_txt = findSubnode(bar, "1.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(one_txt, &metadata->current_backup, NULL, 0);

  PathNode *two_txt = findSubnode(bar, "2.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(two_txt, &metadata->current_backup, NULL, 0);

  PathNode *three_txt = findSubnode(bar, "3.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(three_txt, &metadata->current_backup, NULL, 0);
}

int main(void)
{
  testGroupStart("discovering new files");
  String cwd = getCwd();
  size_t cwd_depth = countPathElements(cwd);

  Metadata *metadata = metadataNew();
  SearchNode *root_node = searchTreeLoad("generated-config-files/backup-search-test.txt");

  initiateBackup(metadata, root_node);
  checkBackupSearchData(metadata, cwd, cwd_depth);
  finishBackup(metadata, "tmp", "tmp/tmp-file");
  checkBackupSearchData(metadata, cwd, cwd_depth);
  assert_true(countFilesInDir("tmp/") == 0);

  testGroupEnd();
}
