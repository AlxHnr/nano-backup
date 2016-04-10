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

#include <unistd.h>

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
  @param hint The backup hint which all the parent nodes must have.

  @return The found node.
*/
static PathNode *findCwdNode(Metadata *metadata, String cwd,
                             BackupHint hint)
{
  for(PathNode *node = metadata->paths; node != NULL; node = node->subnodes)
  {
    if(node->hint != hint)
    {
      die("path has wrong backup hint: \"%s\"", node->path.str);
    }
    else if(node->policy != BPOL_none)
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

/** Safe wrapper around mkdir(). */
static void makeDir(const char *path)
{
  if(mkdir(path, 0755) != 0)
  {
    dieErrno("failed to create directory \"%s\"", path);
  }
}

/** Safe wrapper around symlink(). */
static void makeSymlink(const char *target, const char *linkpath)
{
  if(symlink(target, linkpath) != 0)
  {
    dieErrno("failed to create symlink \"%s\" -> \"%s\"", linkpath, target);
  }
}

/** Generates a dummy file.

  @param path The full or relative path to the dummy file.
  @param content A string containing the desired files content.
  @param repetitions A value describing how often the specified content
  should be repeated.
*/
static void generateFile(const char *path, const char *content,
                         size_t repetitions)
{
  FileStream *stream = sFopenWrite(path);
  size_t content_length = strlen(content);

  for(size_t index = 0; index < repetitions; index++)
  {
    sFwrite(content, content_length, stream);
  }

  sFclose(stream);
}

/** Safe wrapper around remove(). */
static void removePath(const char *path)
{
  if(remove(path) != 0)
  {
    dieErrno("failed to remove \"%s\"", path);
  }
}

/** Wrapper around mustHaveRegular() which takes a stat struct.

  @param node The node which should be checked.
  @param backup The backup to which the given node must point.
  @param hash The expected hash in RegularFileInfo.
  @param slot The expected slot in RegularFileInfo.
  @param stats The struct containing informations about the file.
*/
static void mustHaveRegularStats(PathNode *node, const Backup *backup,
                                 uint64_t size, const uint8_t *hash,
                                 uint8_t slot, struct stat stats)
{
  mustHaveRegular(node, backup, stats.st_uid, stats.st_gid, stats.st_mtime,
                  stats.st_mode, size, hash, slot);
}

/** Simplified wrapper around mustHaveRegularStats(). It extracts additional
  informations via stat() and passes them to mustHaveRegularStats(). */
static void mustHaveRegularStat(PathNode *node, const Backup *backup,
                                uint64_t size, const uint8_t *hash,
                                uint8_t slot)
{
  mustHaveRegularStats(node, backup, size, hash,
                       slot, sStat(node->path.str));
}

/** Simplified wrapper around mustHaveSymlink() which takes a stat struct.

  @param node The node which should be checked.
  @param backup The backup to which the given node must point.
  @param sym_target The target path of the symlink.
  @param stats The struct containing informations about the symlink.
*/
static void mustHaveSymlinkLStats(PathNode *node, const Backup *backup,
                                  const char *sym_target,
                                  struct stat stats)
{
  mustHaveSymlink(node, backup, stats.st_uid, stats.st_gid,
                  stats.st_mtime, sym_target);
}

/** Like mustHaveRegularStat(), but for mustHaveDirectory(). */
static void mustHaveDirectoryStat(PathNode *node, const Backup *backup)
{
  struct stat stats = sStat(node->path.str);
  mustHaveDirectory(node, backup, stats.st_uid, stats.st_gid,
                    stats.st_mtime, stats.st_mode);
}

/** Finds the node "$PWD/tmp/files".

  @param metadata The metadata containing the nodes.
  @param cwd The current working directory.
  @param hint The backup hint which all nodes in the path must have.
  @param subnode_count The amount of subnodes in "files".

  @return The "files" node.
*/
static PathNode *findFilesNode(Metadata *metadata, String cwd_path,
                               BackupHint hint, size_t subnode_count)
{
  PathNode *cwd = findCwdNode(metadata, cwd_path, hint);
  assert_true(cwd->subnodes != NULL);
  assert_true(cwd->subnodes->next == NULL);

  PathNode *tmp = findSubnode(cwd, "tmp", hint, BPOL_none, 1, 1);
  mustHaveDirectoryStat(tmp, &metadata->current_backup);
  PathNode *files = findSubnode(tmp, "files", hint, BPOL_none, 1, subnode_count);
  mustHaveDirectoryStat(files, &metadata->current_backup);

  return files;
}

/** Hashes of various files. */
static const uint8_t three_hash[] =
{
  0x46, 0xbc, 0x4f, 0x20, 0x4c, 0xe9, 0xd0, 0xcd, 0x59, 0xb4,
  0x29, 0xb3, 0x80, 0x7b, 0x64, 0x94, 0xfe, 0x77, 0xf5, 0xfe,
};
static const uint8_t some_file_hash[] =
{
  0x5f, 0x0c, 0xd3, 0x9e, 0xf3, 0x62, 0xdc, 0x1f, 0xe6, 0xd9,
  0x4f, 0xbb, 0x7f, 0xec, 0x8b, 0x9f, 0xb7, 0x86, 0x10, 0x54,
};
static const uint8_t super_hash[] =
{
  0xb7, 0x44, 0x39, 0x8d, 0x17, 0x9e, 0x9d, 0x86, 0x39, 0x3c,
  0x33, 0x49, 0xce, 0x24, 0x06, 0x67, 0x41, 0x89, 0xbb, 0x89,
};

/** Contains the stats of various removed files. */
static struct stat two_txt_stats;
static struct stat link_stats;
static struct stat super_stats;

/** Contains the timestamp at which a phase finished. */
static time_t phase_timestamps[4] = { 0 };

/** Finishes a backup and writes the given metadata struct into "tmp/repo".

  @param metadata The metadata which should be used to finish the backup.
  @param phase The number of the current backup phase minus 1. Needed for
  storing the backup timestamp.
*/
static void completeBackup(Metadata *metadata, size_t phase)
{
  time_t before_finishing = sTime();
  finishBackup(metadata,  "tmp/repo", "tmp/repo/tmp-file");
  time_t after_finishing = sTime();

  assert_true(metadata->current_backup.timestamp >= before_finishing);
  assert_true(metadata->current_backup.timestamp <= after_finishing);
  phase_timestamps[phase] = metadata->current_backup.timestamp;

  metadataWrite(metadata, "tmp/repo", "tmp/repo/tmp-file", "tmp/repo/metadata");
}

/** Performs an initial backup. */
static void runPhase1(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_1_node)
{
  /* Generate dummy files. */
  makeDir("tmp/files/foo");
  makeDir("tmp/files/foo/bar");
  makeDir("tmp/files/foo/dir");
  makeDir("tmp/files/foo/dir/empty");
  generateFile("tmp/files/foo/bar/1.txt", "A small file", 1);
  generateFile("tmp/files/foo/bar/2.txt", "", 0);
  generateFile("tmp/files/foo/bar/3.txt", "This is a test file\n", 20);
  generateFile("tmp/files/foo/some file", "nano-backup ", 7);
  generateFile("tmp/files/foo/dir/3.txt", "This is a test file\n", 20);
  makeSymlink("../some file", "tmp/files/foo/dir/link");
  two_txt_stats = sStat("tmp/files/foo/bar/2.txt");
  link_stats = sLStat("tmp/files/foo/dir/link");

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, phase_1_node);
  assert_true(countFilesInDir("tmp/repo") == 0);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 12);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 12);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 1);
  PathNode *foo = findSubnode(files, "foo", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_added, BPOL_track, 1, 3);
  mustHaveDirectoryStat(bar, &metadata->current_backup);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(one_txt, &metadata->current_backup, 12, NULL, 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStats(two_txt, &metadata->current_backup, 0, NULL, 0, two_txt_stats);
  PathNode *three_txt = findSubnode(bar, "3.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(three_txt, &metadata->current_backup, 400, NULL, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryStat(dir, &metadata->current_backup);
  PathNode *dir_three_txt = findSubnode(dir, "3.txt", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->current_backup, 400, NULL, 0);
  PathNode *empty = findSubnode(dir, "empty", BH_added, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(empty, &metadata->current_backup);
  PathNode *link = findSubnode(dir, "link", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLStats(link, &metadata->current_backup, "../some file", link_stats);

  PathNode *some_file = findSubnode(foo, "some file", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->current_backup, 84, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata, 0);
  mustHaveRegularStat(one_txt,       &metadata->current_backup, 12,  (uint8_t *)"A small file", 0);
  mustHaveRegularStat(two_txt,       &metadata->current_backup, 0,   (uint8_t *)"", 0);
  mustHaveRegularStat(three_txt,     &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(some_file,     &metadata->current_backup, 84,  some_file_hash, 0);
  assert_true(countFilesInDir("tmp/repo") == 3);
}

/** Tests a second backup by creating new files. */
static void runPhase2(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_1_node)
{
  /* Generate dummy files. */
  makeDir("tmp/files/foo/dummy");
  generateFile("tmp/files/foo/super.txt",  "This is a super file\n", 100);
  generateFile("tmp/files/foo/dummy/file", "dummy file", 1);
  super_stats = sStat("tmp/files/foo/super.txt");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 12);
  checkHistPoint(metadata, 0, 0, phase_timestamps[0], cwd_depth + 12);
  initiateBackup(metadata, phase_1_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 7);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[0], 8);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 1);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 5);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryStat(bar, &metadata->backup_history[0]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(one_txt, &metadata->backup_history[0], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(two_txt, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *three_txt = findSubnode(bar, "3.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(three_txt, &metadata->backup_history[0], 400, three_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(dir, &metadata->current_backup);
  PathNode *dir_three_txt = findSubnode(dir, "3.txt", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(empty, &metadata->backup_history[0]);
  PathNode *link = findSubnode(dir, "link", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLStats(link, &metadata->backup_history[0], "../some file", link_stats);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[0], 84, some_file_hash, 0);

  PathNode *super = findSubnode(foo, "super.txt", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStats(super, &metadata->current_backup, 2100, NULL, 0, super_stats);

  PathNode *dummy = findSubnode(foo, "dummy", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(dummy, &metadata->current_backup);
  PathNode *file = findSubnode(dummy, "file", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(file, &metadata->current_backup, 10, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata, 1);
  mustHaveRegularStat(super, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(file,  &metadata->current_backup, 10, (uint8_t *)"dummy file", 0);
  assert_true(countFilesInDir("tmp/repo") == 4);
}

/** Performs a third backup by removing files. */
static void runPhase3(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_3_node)
{
  /* Remove various files. */
  removePath("tmp/files/foo/bar/2.txt");
  removePath("tmp/files/foo/dir/link");
  removePath("tmp/files/foo/super.txt");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[1], cwd_depth + 7);
  checkHistPoint(metadata, 1, 1, phase_timestamps[0], 8);
  initiateBackup(metadata, phase_3_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 6);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[1], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 1);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 5);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryStat(bar, &metadata->backup_history[1]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(one_txt, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->current_backup);
  mustHaveRegularStats(two_txt, &metadata->backup_history[1], 0, (uint8_t *)"", 0, two_txt_stats);
  PathNode *three_txt = findSubnode(bar, "3.txt", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularStat(three_txt, &metadata->backup_history[1], 400, three_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(dir, &metadata->current_backup);
  PathNode *dir_three_txt = findSubnode(dir, "3.txt", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->backup_history[1], 400, three_hash, 0);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(empty, &metadata->backup_history[1]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 2, 0);
  mustHaveNonExisting(link, &metadata->current_backup);
  mustHaveSymlinkLStats(link, &metadata->backup_history[1], "../some file", link_stats);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[1], 84, some_file_hash, 0);

  PathNode *super = findSubnode(foo, "super.txt", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularStats(super, &metadata->backup_history[0], 2100, NULL, 0, super_stats);

  PathNode *dummy = findSubnode(foo, "dummy", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(dummy, &metadata->current_backup);
  PathNode *file = findSubnode(dummy, "file", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularStat(file, &metadata->backup_history[0], 10, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata, 2);
  assert_true(countFilesInDir("tmp/repo") == 4);
}

/** Performs a fourth backup, which doesn't do anything. */
static void runPhase4(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_4_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[2], cwd_depth + 6);
  checkHistPoint(metadata, 1, 1, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_4_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 4);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[2], 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 1);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(bar, &metadata->backup_history[1]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(one_txt, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[0]);
  mustHaveRegularStats(two_txt, &metadata->backup_history[1], 0, (uint8_t *)"", 0, two_txt_stats);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(dir, &metadata->current_backup);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(empty, &metadata->backup_history[1]);
  PathNode *link = findSubnode(dir, "link", BH_unchanged, BPOL_copy, 2, 0);
  mustHaveNonExisting(link, &metadata->backup_history[0]);
  mustHaveSymlinkLStats(link, &metadata->backup_history[1], "../some file", link_stats);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[1], 84, some_file_hash, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata, 3);
  assert_true(countFilesInDir("tmp/repo") == 4);
}

/** Performs a fifth backup by creating various deeply nested files and
  directories. */
static void runPhase5(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_5_node)
{
  /* Remove remains from previous phases. */
  removePath("tmp/files/foo/bar/3.txt");
  removePath("tmp/files/foo/dir/3.txt");
  removePath("tmp/files/foo/dummy/file");
  removePath("tmp/files/foo/dummy");

  /* Generate dummy files. */
  makeDir("tmp/files/foo/bar/subdir");
  makeDir("tmp/files/foo/bar/subdir/a2");
  makeDir("tmp/files/foo/bar/subdir/a2/b");
  makeDir("tmp/files/foo/bar/subdir/a2/b/d");
  makeDir("tmp/files/foo/bar/subdir/a2/b/d/e");
  makeDir("tmp/files/data");
  makeDir("tmp/files/data/a");
  makeDir("tmp/files/data/a/b");
  makeDir("tmp/files/data/a/b/c");
  makeDir("tmp/files/data/a/1");
  makeDir("tmp/files/data/a/1/2");
  makeDir("tmp/files/data/a/1/2/3");
  makeDir("tmp/files/nested");
  makeDir("tmp/files/nested/a");
  makeDir("tmp/files/nested/b");
  makeDir("tmp/files/nested/c");
  makeDir("tmp/files/nested/c/d");
  makeDir("tmp/files/test");
  makeDir("tmp/files/test/a");
  makeDir("tmp/files/test/a/b");
  makeDir("tmp/files/test/a/b/d");
  generateFile("tmp/files/foo/bar/subdir/a1",         "1",            1);
  generateFile("tmp/files/foo/bar/subdir/a2/b/c",     "1",            20);
  generateFile("tmp/files/foo/bar/subdir/a2/b/d/e/f", "Test",         3);
  generateFile("tmp/files/data/a/b/c/d",              "Large\n",      200);
  generateFile("tmp/files/nested/b/1",                "nested-file ", 12);
  generateFile("tmp/files/nested/b/2",                "nested ",      8);
  generateFile("tmp/files/nested/c/d/e",              "Large\n",      200);
  generateFile("tmp/files/test/a/b/c",                "a/b/c/",       7);
  generateFile("tmp/files/test/a/b/d/e",              "FILE CONTENT", 1);
  generateFile("tmp/files/test/a/b/d/f",              "CONTENT",      1);

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[3], cwd_depth + 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_5_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 35);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 41);
  checkHistPoint(metadata, 0, 0, phase_timestamps[3], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryStat(bar, &metadata->backup_history[2]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(one_txt, &metadata->backup_history[2], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[1]);
  mustHaveRegularStats(two_txt, &metadata->backup_history[2], 0, (uint8_t *)"", 0, two_txt_stats);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(dir, &metadata->current_backup);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(empty, &metadata->backup_history[2]);
  PathNode *link = findSubnode(dir, "link", BH_unchanged, BPOL_copy, 2, 0);
  mustHaveNonExisting(link, &metadata->backup_history[1]);
  mustHaveSymlinkLStats(link, &metadata->backup_history[2], "../some file", link_stats);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[2], 84, some_file_hash, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata, 3);
  assert_true(countFilesInDir("tmp/repo") == 8);
}

int main(void)
{
  testGroupStart("prepare backup");
  String cwd = getCwd();
  size_t cwd_depth = countPathElements(cwd);
  SearchNode *phase_1_node = searchTreeLoad("generated-config-files/backup-phase-1.txt");
  SearchNode *phase_3_node = searchTreeLoad("generated-config-files/backup-phase-3.txt");
  SearchNode *phase_4_node = searchTreeLoad("generated-config-files/backup-phase-4.txt");
  SearchNode *phase_5_node = searchTreeLoad("generated-config-files/backup-phase-5.txt");
  makeDir("tmp/repo");
  makeDir("tmp/files");
  testGroupEnd();

  testGroupStart("initial backup");
  runPhase1(cwd, cwd_depth, phase_1_node);
  testGroupEnd();

  testGroupStart("discovering new files");
  runPhase2(cwd, cwd_depth, phase_1_node);
  testGroupEnd();

  testGroupStart("removing files");
  runPhase3(cwd, cwd_depth, phase_3_node);
  testGroupEnd();

  testGroupStart("backup with no changes");
  runPhase4(cwd, cwd_depth, phase_4_node);
  testGroupEnd();

  testGroupStart("generating nested files and directories");
  runPhase5(cwd, cwd_depth, phase_5_node);
  testGroupEnd();
}
