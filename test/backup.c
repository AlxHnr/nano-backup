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
  Tests the core backup logic.
*/

#include "backup.h"

#include <unistd.h>
#include <stdlib.h>

#include "test.h"
#include "restore.h"
#include "metadata.h"
#include "memory-pool.h"
#include "search-tree.h"
#include "test-common.h"
#include "path-builder.h"
#include "string-table.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** Finds the node that represents the directory in which this test runs.
  It will terminate the program if the node doesn't exist, or its parent
  nodes are invalid.

  @param metadata The metadata containing the nodes. It must be a valid
  metadata structure, so make sure to pass it to checkMetadata() first.
  @param cwd The current working directory.
  @param hint The backup hint which all the parent nodes must have.
  Timestamp changes will be ignored.

  @return The found node.
*/
static PathNode *findCwdNode(Metadata *metadata, String cwd,
                             BackupHint hint)
{
  for(PathNode *node = metadata->paths; node != NULL; node = node->subnodes)
  {
    if((node->hint & ~BH_timestamp_changed) != hint)
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
  @param hint The BackupHint which the requested node should have.
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

/** Creates a backup of the given paths parent directories timestamps. */
static time_t getParentTime(const char *path)
{
  return sStat(strCopy(strSplitPath(str(path)).head).str).st_mtime;
}

/** Counterpart to getParentTime(). */
static void restoreParentTime(const char *path, time_t time)
{
  const char *parent_path = strCopy(strSplitPath(str(path)).head).str;
  sUtime(parent_path, time);
}

/** Safe wrapper around mkdir(). */
static void makeDir(const char *path)
{
  time_t parent_time = getParentTime(path);
  sMkdir(path);
  restoreParentTime(path, parent_time);
}

/** Safe wrapper around symlink(). */
static void makeSymlink(const char *target, const char *linkpath)
{
  time_t parent_time = getParentTime(linkpath);
  sSymlink(target, linkpath);
  restoreParentTime(linkpath, parent_time);
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
  if(sPathExists(path))
  {
    die("failed to generate file: Already existing: \"%s\"", path);
  }

  time_t parent_time = getParentTime(path);
  FileStream *stream = sFopenWrite(path);
  size_t content_length = strlen(content);

  for(size_t index = 0; index < repetitions; index++)
  {
    sFwrite(content, content_length, stream);
  }

  sFclose(stream);
  restoreParentTime(path, parent_time);
}

/** Generates dummy files and stores them with an invalid unique name in
  "tmp/repo". This causes hash collisions.

  @param hash The hash for which the collisions should be generated.
  @param size The size of the colliding file.
  @param files_to_create The amount of files to create. Can't be greater
  than 256.
*/
static void generateCollidingFiles(const uint8_t *hash, size_t size,
                                   size_t files_to_create)
{
  assert_true(files_to_create <= UINT8_MAX + 1);

  RegularFileInfo info;
  memcpy(info.hash, hash, FILE_HASH_SIZE);
  info.size = size;
  info.slot = 0;

  static Buffer *path_buffer = NULL;
  pathBuilderSet(&path_buffer, "tmp/repo");

  static Buffer *path_in_repo = NULL;
  repoBuildRegularFilePath(&path_in_repo, &info);
  pathBuilderAppend(&path_buffer, 8, path_in_repo->data);

  path_buffer->data[13] = '\0';
  if(sPathExists(path_buffer->data) == false)
  {
    path_buffer->data[10] = '\0';
    if(sPathExists(path_buffer->data) == false)
    {
      sMkdir(path_buffer->data);
    }
    path_buffer->data[10] = '/';

    sMkdir(path_buffer->data);
  }
  path_buffer->data[13] = '/';

  for(size_t slot = 0; slot < files_to_create; slot++)
  {
    info.slot = (uint8_t)slot;
    repoBuildRegularFilePath(&path_in_repo, &info);
    pathBuilderAppend(&path_buffer, 8, path_in_repo->data);
    FileStream *stream = sFopenWrite(path_buffer->data);

    const uint8_t bytes_to_write[] = { info.slot, 0 };
    size_t bytes_left = size;
    while(bytes_left >= 2)
    {
      sFwrite(bytes_to_write, 2, stream);
      bytes_left -= 2;
    }
    if(bytes_left)
    {
      sFwrite(bytes_to_write, 1, stream);
    }

    sFclose(stream);
  }
}

/** Safe wrapper around remove(). */
static void removePath(const char *path)
{
  time_t parent_time = getParentTime(path);
  sRemove(path);
  restoreParentTime(path, parent_time);
}

/** Like generateFile(), but overwrites an existing file without affecting
  its modification timestamp.

  @param node The node containing the path to update. It must represent a
  regular file at its current backup point.
  @param content The content of the file to generate.
  @param repetitions Contains how many times the given content should be
  repeated.
*/
static void regenerateFile(PathNode *node, const char *content,
                           size_t repetitions)
{
  assert_true(node->history->state.type == PST_regular);

  removePath(node->path.str);
  generateFile(node->path.str, content, repetitions);
  sUtime(node->path.str, node->history->state.metadata.reg.timestamp);
}

/** Changes the path to which a symlink points.

  @param new_target The new target path to which the symlink points.
  @param linkpath The path to the symlink to update.
*/
static void remakeSymlink(const char *new_target, const char *linkpath)
{
  removePath(linkpath);
  makeSymlink(new_target, linkpath);
}

/** Asserts that "tmp" contains only "repo" and "files". */
static void assertTmpIsCleared(void)
{
  sRemoveRecursively("tmp");
  sMkdir("tmp");
  sMkdir("tmp/repo");
  sMkdir("tmp/files");
}

/** Finds the first point in the nodes history, which is not
  PST_non_existing. */
static PathHistory *findExistingHistPoint(PathNode *node)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->state.type != PST_non_existing)
    {
      return point;
    }
  }

  die("failed to find existing path state type for \"%s\"",
      node->path.str);
  return NULL;
}

/** Restores a regular file with its modification timestamp.

  @param path The path to the file.
  @param info The file info of the state to which the file should be
  restored to.
*/
static void restoreRegularFile(const char *path,
                               const RegularFileInfo *info)
{
  time_t parent_time = getParentTime(path);

  restoreFile(path, info, "tmp/repo");
  sUtime(path, info->timestamp);

  restoreParentTime(path, parent_time);
}

/** Restores the files in the given PathNode recursively to their last
  existing state. It also restores modification timestamps.

  @param node The node to restore.
*/
static void restoreWithTimeRecursively(PathNode *node)
{
  if(sPathExists(node->path.str) == false)
  {
    PathHistory *point = findExistingHistPoint(node);
    switch(point->state.type)
    {
      case PST_regular:
        restoreRegularFile(node->path.str, &point->state.metadata.reg);
        break;
      case PST_symlink:
        makeSymlink(point->state.metadata.sym_target, node->path.str);
        break;
      case PST_directory:
        makeDir(node->path.str);
        sUtime(node->path.str, point->state.metadata.dir.timestamp);
        break;
      default:
        die("unable to restore \"%s\"", node->path.str);
    }
  }

  if(S_ISDIR(sLStat(node->path.str).st_mode))
  {
    for(PathNode *subnode = node->subnodes;
        subnode != NULL; subnode = subnode->next)
    {
      restoreWithTimeRecursively(subnode);
    }
  }
}

/** Associates a file path with its stats. */
static StringTable *stat_cache = NULL;

/** Stats a file and caches the result for subsequent runs.

  @param path The path to the file to stat. Must contain a null-terminated
  buffer.
  @param stat_fun The stat function to use.

  @return The stats which the given path had on its first access trough
  this function.
*/
static struct stat cachedStat(String path, struct stat (*stat_fun)(const char *))
{
  struct stat *cache = strTableGet(stat_cache, path);
  if(cache == NULL)
  {
    cache = mpAlloc(sizeof *cache);
    *cache = stat_fun(path.str);
    strTableMap(stat_cache, path, cache);
  }

  return *cache;
}

/** Resets the stat cache. */
static void resetStatCache(void)
{
  strTableFree(stat_cache);
  stat_cache = strTableNew();
}

/** Like mustHaveRegular(), but takes a stat struct instead. */
static void mustHaveRegularStats(PathNode *node, const Backup *backup,
                                 struct stat stats, uint64_t size,
                                 const uint8_t *hash, uint8_t slot)
{
  mustHaveRegular(node, backup, stats.st_uid, stats.st_gid, stats.st_mtime,
                  stats.st_mode, size, hash, slot);
}

/** Wrapper around mustHaveRegular(), which extracts additional
  informations using sStat(). */
static void mustHaveRegularStat(PathNode *node, const Backup *backup,
                                uint64_t size, const uint8_t *hash,
                                uint8_t slot)
{
  mustHaveRegularStats(node, backup, sStat(node->path.str),
                       size, hash, slot);
}

/** Cached version of mustHaveRegularStat(). */
static void mustHaveRegularCached(PathNode *node, const Backup *backup,
                                  uint64_t size, const uint8_t *hash,
                                  uint8_t slot)
{
  mustHaveRegularStats(node, backup, cachedStat(node->path, sStat),
                       size, hash, slot);
}

/** Like mustHaveSymlinkLStat(), but takes a stat struct instead. */
static void mustHaveSymlinkStats(PathNode *node, const Backup *backup,
                                 struct stat stats, const char *sym_target)
{
  mustHaveSymlink(node, backup, stats.st_uid, stats.st_gid, sym_target);
}

/** Like mustHaveRegularStat(), but for mustHaveSymlink(). */
static void mustHaveSymlinkLStat(PathNode *node, const Backup *backup,
                                  const char *sym_target)
{
  mustHaveSymlinkStats(node, backup, sLStat(node->path.str), sym_target);
}

/** Cached version of mustHaveSymlinkLStat(). */
static void mustHaveSymlinkLCached(PathNode *node, const Backup *backup,
                                   const char *sym_target)
{
  mustHaveSymlinkStats(node, backup, cachedStat(node->path, sLStat),
                       sym_target);
}

/** Like mustHaveDirectory, but takes a stat struct instead. */
static void mustHaveDirectoryStats(PathNode *node, const Backup *backup,
                                   struct stat stats)
{
  mustHaveDirectory(node, backup, stats.st_uid, stats.st_gid,
                    stats.st_mtime, stats.st_mode);
}

/** Like mustHaveRegularStat(), but for mustHaveDirectory(). */
static void mustHaveDirectoryStat(PathNode *node, const Backup *backup)
{
  mustHaveDirectoryStats(node, backup, sStat(node->path.str));
}

/** Cached version of mustHaveDirectoryStat(). */
static void mustHaveDirectoryCached(PathNode *node, const Backup *backup)
{
  mustHaveDirectoryStats(node, backup, cachedStat(node->path, sStat));
}

/** Finds the node "$PWD/tmp/files".

  @param metadata The metadata containing the nodes.
  @param cwd_path The current working directory.
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
static const uint8_t data_d_hash[] =
{
  0xd8, 0x26, 0xd3, 0x91, 0xc7, 0xdc, 0x38, 0xd3, 0x7f, 0x73,
  0x79, 0x61, 0x68, 0xe5, 0x58, 0x1f, 0x7b, 0x99, 0x82, 0xd3,
};
static const uint8_t nested_1_hash[] =
{
  0xaf, 0x07, 0xcc, 0xfe, 0xf5, 0x5c, 0x44, 0x94, 0x7b, 0x63,
  0x0f, 0x58, 0xe8, 0x2a, 0xb0, 0x42, 0xca, 0x68, 0x94, 0xb8,
};
static const uint8_t nested_2_hash[] =
{
  0x71, 0xe6, 0x14, 0x82, 0xbf, 0xd5, 0x93, 0x01, 0x41, 0x83,
  0xa2, 0x5e, 0x66, 0x02, 0xa9, 0x0f, 0x8d, 0xbc, 0x74, 0x0f,
};
static const uint8_t test_c_hash[] =
{
  0x2b, 0x85, 0xa2, 0xb0, 0x6e, 0x49, 0x8c, 0x7b, 0x97, 0x6d,
  0xa4, 0xff, 0x8d, 0x34, 0xed, 0x84, 0xcb, 0x42, 0xc7, 0xe0,
};
static const uint8_t nb_manual_b_hash[] =
{
  0xcf, 0x71, 0xd9, 0x92, 0xf9, 0x69, 0xb2, 0x1d, 0x31, 0x94,
  0x06, 0x46, 0xdc, 0x6e, 0x5d, 0xe6, 0xd4, 0xaf, 0x2f, 0xa1,
};
static const uint8_t nb_a_abc_1_hash[] =
{
  0x55, 0x71, 0x58, 0x4d, 0xeb, 0x0a, 0x98, 0xdc, 0xbd, 0xa1,
  0x5d, 0xc9, 0xda, 0x9f, 0xfe, 0x10, 0x01, 0xe2, 0xb5, 0xfe,
};
static const uint8_t bin_hash[] =
{
  0x6c, 0x88, 0xdb, 0x41, 0xc1, 0xb2, 0xb2, 0x6a, 0xa7, 0xa8,
  0xd5, 0xd9, 0x4a, 0xbd, 0xf2, 0x0b, 0x39, 0x76, 0xd9, 0x61,
};
static const uint8_t bin_c_1_hash[] =
{
  0xe8, 0xfb, 0x29, 0x61, 0x97, 0x00, 0xe5, 0xb6, 0x09, 0x30,
  0x88, 0x6e, 0x94, 0x82, 0x2c, 0x66, 0xce, 0x2a, 0xd6, 0xbf,
};
static const uint8_t node_24_hash[] =
{
  0x18, 0x3b, 0x8a, 0x27, 0xe5, 0xc0, 0xc6, 0x0c, 0x60, 0x1a,
  0xb8, 0x0b, 0xb5, 0x50, 0xa3, 0x8c, 0x0b, 0xd1, 0x42, 0x6a,
};
static const uint8_t node_26_hash[] =
{
  0x07, 0x8c, 0x51, 0x64, 0x00, 0x36, 0xaa, 0x01, 0x6e, 0x40,
  0xef, 0x9f, 0x1f, 0xd6, 0x0e, 0xfe, 0xe3, 0xac, 0xa6, 0xdb,
};
static const uint8_t node_28_hash[] =
{
  0x24, 0xf1, 0x18, 0x86, 0x65, 0x5f, 0xba, 0xec, 0x06, 0x5d,
  0x80, 0xcb, 0xfe, 0x62, 0x19, 0x95, 0x3c, 0x8c, 0x1a, 0xa4,
};
static const uint8_t node_29_hash[] =
{
  0xd1, 0x56, 0x90, 0xc2, 0x79, 0x90, 0x92, 0xdd, 0x2f, 0x5d,
  0x58, 0x60, 0x39, 0x18, 0x07, 0x11, 0xe5, 0xa3, 0x13, 0x5a,
};
static const uint8_t node_42_hash[] =
{
  0x10, 0xec, 0x41, 0x8f, 0xd4, 0xd4, 0x55, 0x1d, 0xfe, 0x9c,
  0xe1, 0x3a, 0x99, 0x6e, 0x9b, 0x30, 0x62, 0x39, 0x42, 0xe9,
};
static const uint8_t node_45_hash[] =
{
  0x78, 0xa5, 0x60, 0xf4, 0x74, 0x2d, 0xfe, 0x37, 0x32, 0x4c,
  0x2b, 0x66, 0x80, 0x1f, 0x3f, 0x45, 0xce, 0x03, 0xe2, 0xef,
};
static const uint8_t node_46_hash[] =
{
  0x21, 0x1d, 0x56, 0xce, 0xad, 0xb7, 0xe7, 0x81, 0x1e, 0x08,
  0x2d, 0x09, 0x57, 0x4e, 0x5c, 0x02, 0x15, 0x47, 0xa8, 0xf5,
};

/** Contains the timestamp at which a phase finished. */
static time_t *phase_timestamps = NULL;
static size_t backup_counter = 0;

/** Finishes a backup and writes the given metadata struct into "tmp/repo".
  It additionally stores the backup timestamp in "phase_timestamps".

  @param metadata The metadata which should be used to finish the backup.
*/
static void completeBackup(Metadata *metadata)
{
  size_t phase = backup_counter;
  backup_counter++;

  phase_timestamps =
    sRealloc(phase_timestamps, sizeof *phase_timestamps * backup_counter);

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
  assertTmpIsCleared();
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

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, phase_1_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 12);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 12);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 1);
  PathNode *foo = findSubnode(files, "foo", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_added, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->current_backup);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->current_backup, 12, NULL, 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(two_txt, &metadata->current_backup, 0, NULL, 0);
  PathNode *three_txt = findSubnode(bar, "3.txt", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(three_txt, &metadata->current_backup, 400, NULL, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *dir_three_txt = findSubnode(dir, "3.txt", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->current_backup, 400, NULL, 0);
  PathNode *empty = findSubnode(dir, "empty", BH_added, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->current_backup);
  PathNode *link = findSubnode(dir, "link", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->current_backup, "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->current_backup, 84, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  mustHaveRegularStat(one_txt,       &metadata->current_backup, 12,  (uint8_t *)"A small file", 0);
  mustHaveRegularStat(two_txt,       &metadata->current_backup, 0,   (uint8_t *)"", 0);
  mustHaveRegularStat(three_txt,     &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(some_file,     &metadata->current_backup, 84,  some_file_hash, 0);
  assert_true(countItemsInDir("tmp/repo") == 7);
}

/** Tests a second backup by creating new files. */
static void runPhase2(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_1_node)
{
  /* Generate dummy files. */
  makeDir("tmp/files/foo/dummy");
  generateFile("tmp/files/foo/super.txt",  "This is a super file\n", 100);
  generateFile("tmp/files/foo/dummy/file", "dummy file", 1);

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
  mustHaveDirectoryCached(bar, &metadata->backup_history[0]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[0], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(two_txt, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *three_txt = findSubnode(bar, "3.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(three_txt, &metadata->backup_history[0], 400, three_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *dir_three_txt = findSubnode(dir, "3.txt", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[0]);
  PathNode *link = findSubnode(dir, "link", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[0], "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[0], 84, some_file_hash, 0);

  PathNode *super = findSubnode(foo, "super.txt", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(super, &metadata->current_backup, 2100, NULL, 0);

  PathNode *dummy = findSubnode(foo, "dummy", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(dummy, &metadata->current_backup);
  PathNode *file = findSubnode(dummy, "file", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(file, &metadata->current_backup, 10, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  mustHaveRegularStat(super, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(file,  &metadata->current_backup, 10, (uint8_t *)"dummy file", 0);
  assert_true(countItemsInDir("tmp/repo") == 10);
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
  assert_true(metadata->current_backup.ref_count == cwd_depth + 5);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[1], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 1);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 5);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[1]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->current_backup);
  mustHaveRegularCached(two_txt, &metadata->backup_history[1], 0, (uint8_t *)"", 0);
  PathNode *three_txt = findSubnode(bar, "3.txt", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularStat(three_txt, &metadata->backup_history[1], 400, three_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *dir_three_txt = findSubnode(dir, "3.txt", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularStat(dir_three_txt, &metadata->backup_history[1], 400, three_hash, 0);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[1]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[1], "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[1], 84, some_file_hash, 0);

  PathNode *super = findSubnode(foo, "super.txt", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(super, &metadata->backup_history[0], 2100, NULL, 0);

  PathNode *dummy = findSubnode(foo, "dummy", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(dummy, &metadata->current_backup);
  PathNode *file = findSubnode(dummy, "file", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularStat(file, &metadata->backup_history[0], 10, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 10);
}

/** Performs a fourth backup, which doesn't do anything. */
static void runPhase4(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_4_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[2], cwd_depth + 5);
  checkHistPoint(metadata, 1, 1, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_4_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 4);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[2], 1);
  checkHistPoint(metadata, 1, 1, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 1);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bar, &metadata->backup_history[1]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[0]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[1]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[1], "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[1], 84, some_file_hash, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 10);

  /* Clean up after test. */
  removePath("tmp/files/foo/bar/3.txt");
  removePath("tmp/files/foo/dir/3.txt");
  removePath("tmp/files/foo/dummy/file");
  removePath("tmp/files/foo/dummy");
}

/** Performs a fifth backup by creating various deeply nested files and
  directories. */
static void runPhase5(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_5_node)
{
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
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_5_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 35);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 41);
  checkHistPoint(metadata, 0, 0, phase_timestamps[3], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[2]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[2], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[1]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[2], 0, (uint8_t *)"", 0);

  PathNode *subdir = findSubnode(bar, "subdir", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryStat(subdir, &metadata->current_backup);
  PathNode *subdir_a1 = findSubnode(subdir, "a1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(subdir_a1, &metadata->current_backup, 1, NULL, 0);
  PathNode *subdir_a2 = findSubnode(subdir, "a2", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryStat(subdir_a2, &metadata->current_backup);
  PathNode *subdir_b = findSubnode(subdir_a2, "b", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryStat(subdir_b, &metadata->current_backup);
  PathNode *subdir_c = findSubnode(subdir_b, "c", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(subdir_c, &metadata->current_backup, 20, NULL, 0);
  PathNode *subdir_d = findSubnode(subdir_b, "d", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryStat(subdir_d, &metadata->current_backup);
  PathNode *subdir_e = findSubnode(subdir_d, "e", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryStat(subdir_e, &metadata->current_backup);
  PathNode *subdir_f = findSubnode(subdir_e, "f", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(subdir_f, &metadata->current_backup, 12, NULL, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[2]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[2], "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[2], 84, some_file_hash, 0);

  PathNode *data = findSubnode(files, "data", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data, &metadata->current_backup);
  PathNode *data_a = findSubnode(data, "a", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(data_a, &metadata->current_backup);
  PathNode *data_b = findSubnode(data_a, "b", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_b, &metadata->current_backup);
  PathNode *data_c = findSubnode(data_b, "c", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_c, &metadata->current_backup);
  PathNode *data_d = findSubnode(data_c, "d", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(data_d, &metadata->current_backup, 1200, NULL, 0);
  PathNode *data_1 = findSubnode(data_a, "1", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_1, &metadata->current_backup);
  PathNode *data_2 = findSubnode(data_1, "2", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_2, &metadata->current_backup);
  PathNode *data_3 = findSubnode(data_2, "3", BH_added, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(data_3, &metadata->current_backup);

  PathNode *nested = findSubnode(files, "nested", BH_added, BPOL_copy, 1, 3);
  mustHaveDirectoryStat(nested, &metadata->current_backup);
  PathNode *nested_a = findSubnode(nested, "a", BH_added, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(nested_a, &metadata->current_backup);
  PathNode *nested_b = findSubnode(nested, "b", BH_added, BPOL_copy, 1, 2);
  mustHaveDirectoryStat(nested_b, &metadata->current_backup);
  PathNode *nested_1 = findSubnode(nested_b, "1", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(nested_1, &metadata->current_backup, 144, NULL, 0);
  PathNode *nested_2 = findSubnode(nested_b, "2", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(nested_2, &metadata->current_backup, 56, NULL, 0);
  PathNode *nested_c = findSubnode(nested, "c", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(nested_c, &metadata->current_backup);
  PathNode *nested_d = findSubnode(nested_c, "d", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(nested_d, &metadata->current_backup);
  PathNode *nested_e = findSubnode(nested_d, "e", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(nested_e, &metadata->current_backup, 1200, NULL, 0);

  PathNode *test = findSubnode(files, "test", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(test, &metadata->current_backup);
  PathNode *test_a = findSubnode(test, "a", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(test_a, &metadata->current_backup);
  PathNode *test_b = findSubnode(test_a, "b", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(test_b, &metadata->current_backup);
  PathNode *test_c = findSubnode(test_b, "c", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(test_c, &metadata->current_backup, 42, NULL, 0);
  PathNode *test_d = findSubnode(test_b, "d", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(test_d, &metadata->current_backup);
  PathNode *test_e = findSubnode(test_d, "e", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(test_e, &metadata->current_backup, 12, NULL, 0);
  PathNode *test_f = findSubnode(test_d, "f", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(test_f, &metadata->current_backup, 7, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 22);
  mustHaveRegularStat(subdir_a1, &metadata->current_backup, 1,    (uint8_t *)"1???????????????????", 0);
  mustHaveRegularStat(subdir_c,  &metadata->current_backup, 20,   (uint8_t *)"11111111111111111111", 0);
  mustHaveRegularStat(subdir_f,  &metadata->current_backup, 12,   (uint8_t *)"TestTestTest????????", 0);
  mustHaveRegularCached(data_d,  &metadata->current_backup, 1200, data_d_hash,                       0);
  mustHaveRegularStat(nested_1,  &metadata->current_backup, 144,  nested_1_hash,                     0);
  mustHaveRegularStat(nested_2,  &metadata->current_backup, 56,   nested_2_hash,                     0);
  mustHaveRegularStat(nested_e,  &metadata->current_backup, 1200, data_d_hash,                       0);
  mustHaveRegularCached(test_c,  &metadata->current_backup, 42,   test_c_hash,                       0);
  mustHaveRegularCached(test_e,  &metadata->current_backup, 12,   (uint8_t *)"FILE CONTENT????????", 0);
  mustHaveRegularCached(test_f,  &metadata->current_backup, 7,    (uint8_t *)"CONTENT?????????????", 0);
}

/** Performs a backup after removing various deeply nested files. */
static void runPhase6(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_6_node)
{
  /* Remove various files. */
  removePath("tmp/files/data/a/b/c/d");
  removePath("tmp/files/data/a/b/c");
  removePath("tmp/files/data/a/b");
  removePath("tmp/files/data/a/1/2/3");
  removePath("tmp/files/data/a/1/2");
  removePath("tmp/files/data/a/1");
  removePath("tmp/files/data/a");
  removePath("tmp/files/data");
  removePath("tmp/files/test/a/b/c");
  removePath("tmp/files/test/a/b/d/e");
  removePath("tmp/files/test/a/b/d/f");
  removePath("tmp/files/test/a/b/d");
  removePath("tmp/files/test/a/b");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 41);
  checkHistPoint(metadata, 0, 0, phase_timestamps[4], cwd_depth + 35);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_6_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 4);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 12);
  checkHistPoint(metadata, 0, 0, phase_timestamps[4], 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[2]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[2], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[1]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[2], 0, (uint8_t *)"???", 0);

  PathNode *subdir = findSubnode(bar, "subdir", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryStat(subdir, &metadata->backup_history[0]);
  PathNode *subdir_a1 = findSubnode(subdir, "a1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularStat(subdir_a1, &metadata->backup_history[0], 1, (uint8_t *)"1???????????????????", 0);
  PathNode *subdir_a2 = findSubnode(subdir, "a2", BH_not_part_of_repository, BPOL_track, 1, 1);
  mustHaveDirectoryStat(subdir_a2, &metadata->backup_history[0]);
  PathNode *subdir_b = findSubnode(subdir_a2, "b", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryStat(subdir_b, &metadata->backup_history[0]);
  PathNode *subdir_c = findSubnode(subdir_b, "c", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularStat(subdir_c, &metadata->backup_history[0], 20, (uint8_t *)"11111111111111111111", 0);
  PathNode *subdir_d = findSubnode(subdir_b, "d", BH_not_part_of_repository, BPOL_track, 1, 1);
  mustHaveDirectoryStat(subdir_d, &metadata->backup_history[0]);
  PathNode *subdir_e = findSubnode(subdir_d, "e", BH_not_part_of_repository, BPOL_track, 1, 1);
  mustHaveDirectoryStat(subdir_e, &metadata->backup_history[0]);
  PathNode *subdir_f = findSubnode(subdir_e, "f", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularStat(subdir_f, &metadata->backup_history[0], 12, (uint8_t *)"TestTestTest????????", 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[2]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[2], "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[2], 84, some_file_hash, 0);

  PathNode *data = findSubnode(files, "data", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data, &metadata->backup_history[0]);
  PathNode *data_a = findSubnode(data, "a", BH_not_part_of_repository, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(data_a, &metadata->backup_history[0]);
  PathNode *data_b = findSubnode(data_a, "b", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_b, &metadata->backup_history[0]);
  PathNode *data_c = findSubnode(data_b, "c", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_c, &metadata->backup_history[0]);
  PathNode *data_d = findSubnode(data_c, "d", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(data_d, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *data_1 = findSubnode(data_a, "1", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_1, &metadata->backup_history[0]);
  PathNode *data_2 = findSubnode(data_1, "2", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(data_2, &metadata->backup_history[0]);
  PathNode *data_3 = findSubnode(data_2, "3", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(data_3, &metadata->backup_history[0]);

  PathNode *nested = findSubnode(files, "nested", BH_not_part_of_repository, BPOL_copy, 1, 3);
  mustHaveDirectoryStat(nested, &metadata->backup_history[0]);
  PathNode *nested_a = findSubnode(nested, "a", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(nested_a, &metadata->backup_history[0]);
  PathNode *nested_b = findSubnode(nested, "b", BH_not_part_of_repository, BPOL_copy, 1, 2);
  mustHaveDirectoryStat(nested_b, &metadata->backup_history[0]);
  PathNode *nested_1 = findSubnode(nested_b, "1", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularStat(nested_1, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *nested_2 = findSubnode(nested_b, "2", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularStat(nested_2, &metadata->backup_history[0], 56, nested_2_hash, 0);
  PathNode *nested_c = findSubnode(nested, "c", BH_not_part_of_repository, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(nested_c, &metadata->backup_history[0]);
  PathNode *nested_d = findSubnode(nested_c, "d", BH_not_part_of_repository, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(nested_d, &metadata->backup_history[0]);
  PathNode *nested_e = findSubnode(nested_d, "e", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularStat(nested_e, &metadata->backup_history[0], 1200, data_d_hash, 0);

  PathNode *test = findSubnode(files, "test", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(test, &metadata->backup_history[0]);
  PathNode *test_a = findSubnode(test, "a", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(test_a, &metadata->backup_history[0]);
  PathNode *test_b = findSubnode(test_a, "b", BH_not_part_of_repository, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(test_b, &metadata->backup_history[0]);
  PathNode *test_c = findSubnode(test_b, "c", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(test_c, &metadata->backup_history[0], 42, test_c_hash, 0);
  PathNode *test_d = findSubnode(test_b, "d", BH_not_part_of_repository, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(test_d, &metadata->backup_history[0]);
  PathNode *test_e = findSubnode(test_d, "e", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(test_e, &metadata->backup_history[0], 12, (uint8_t *)"FILE CONTENT????????", 0);
  PathNode *test_f = findSubnode(test_d, "f", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(test_f, &metadata->backup_history[0], 7, (uint8_t *)"CONTENT?????????????", 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 22);

  /* Clean up after test. */
  removePath("tmp/files/foo/bar/subdir/a1");
  removePath("tmp/files/foo/bar/subdir/a2/b/c");
  removePath("tmp/files/foo/bar/subdir/a2/b/d/e/f");
  removePath("tmp/files/foo/bar/subdir/a2/b/d/e");
  removePath("tmp/files/foo/bar/subdir/a2/b/d");
  removePath("tmp/files/foo/bar/subdir/a2/b");
  removePath("tmp/files/foo/bar/subdir/a2");
  removePath("tmp/files/foo/bar/subdir");
  removePath("tmp/files/nested/a");
  removePath("tmp/files/nested/b/1");
  removePath("tmp/files/nested/b/2");
  removePath("tmp/files/nested/b");
  removePath("tmp/files/nested/c/d/e");
  removePath("tmp/files/nested/c/d");
  removePath("tmp/files/nested/c");
  removePath("tmp/files/nested");
}

/** Creates more nested files. */
static void runPhase7(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_7_node)
{
  /* Generate dummy files. */
  makeDir("tmp/files/unneeded");
  makeDir("tmp/files/unneeded/directory");
  makeDir("tmp/files/unneeded/directory/a");
  makeDir("tmp/files/unneeded/directory/a/b");
  makeDir("tmp/files/unneeded/directory/a/e");
  makeDir("tmp/files/unneeded/directory/a/g");
  makeDir("tmp/files/unneeded/directory/a/g/h");
  generateFile("tmp/files/unneeded/directory/a/b/c", "Content", 2);
  generateFile("tmp/files/unneeded/directory/a/e/f", "File",    4);
  makeSymlink("../../b/c", "tmp/files/unneeded/directory/a/g/h/i");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 12);
  checkHistPoint(metadata, 0, 0, phase_timestamps[5], cwd_depth + 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[4], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_7_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 14);
  assert_true(metadata->backup_history_length == 4);
  assert_true(metadata->total_path_count == cwd_depth + 22);
  checkHistPoint(metadata, 0, 0, phase_timestamps[5], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[4], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 3);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bar, &metadata->backup_history[3]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[3], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[2]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[3], 0, (uint8_t *)"???", 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[3]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[3], "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[3], 84, some_file_hash, 0);

  PathNode *test = findSubnode(files, "test", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(test, &metadata->backup_history[1]);
  PathNode *test_a = findSubnode(test, "a", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(test_a, &metadata->backup_history[1]);

  PathNode *unneeded = findSubnode(files, "unneeded", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(unneeded, &metadata->current_backup);
  PathNode *directory = findSubnode(unneeded, "directory", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory, &metadata->current_backup);
  PathNode *directory_a = findSubnode(directory, "a", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryStat(directory_a, &metadata->current_backup);
  PathNode *directory_b = findSubnode(directory_a, "b", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory_b, &metadata->current_backup);
  PathNode *directory_c = findSubnode(directory_b, "c", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(directory_c, &metadata->current_backup, 14, NULL, 0);
  PathNode *directory_e = findSubnode(directory_a, "e", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(directory_e, &metadata->current_backup);
  PathNode *directory_f = findSubnode(directory_e, "f", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(directory_f, &metadata->current_backup, 16, NULL, 0);
  PathNode *directory_g = findSubnode(directory_a, "g", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory_g, &metadata->current_backup);
  PathNode *directory_h = findSubnode(directory_g, "h", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory_h, &metadata->current_backup);
  PathNode *directory_i = findSubnode(directory_h, "i", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLStat(directory_i, &metadata->current_backup, "../../b/c");

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 22);
  mustHaveRegularCached(directory_c, &metadata->current_backup, 14,
                       (uint8_t *)"ContentContent??????", 0);
  mustHaveRegularCached(directory_f, &metadata->current_backup, 16,
                       (uint8_t *)"FileFileFileFile????", 0);
}

/** Tests how unneeded nodes get wiped. */
static void runPhase8(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_8_node)
{
  /* Remove various files. */
  removePath("tmp/files/unneeded/directory/a/b/c");
  removePath("tmp/files/unneeded/directory/a/e/f");
  removePath("tmp/files/unneeded/directory/a/e");
  removePath("tmp/files/test/a");
  removePath("tmp/files/test");

  /* Generate dummy files. */
  makeDir("tmp/files/home");
  makeDir("tmp/files/home/user");
  makeDir("tmp/files/unneeded/directory/a/d");
  generateFile("tmp/files/home/user/text.txt", "0xff\n", 500);

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 22);
  checkHistPoint(metadata, 0, 0, phase_timestamps[6], cwd_depth + 14);
  checkHistPoint(metadata, 1, 1, phase_timestamps[4], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_8_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 4);
  assert_true(metadata->backup_history_length == 4);
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[6], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[4], 0);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bar, &metadata->backup_history[3]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[3], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[2]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[3], 0, (uint8_t *)"???", 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[3]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[3], "../some file");

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[3], 84, some_file_hash, 0);

  PathNode *test = findSubnode(files, "test", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(test, &metadata->backup_history[1]);
  PathNode *test_a = findSubnode(test, "a", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(test_a, &metadata->backup_history[1]);

  PathNode *home = findSubnode(files, "home", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(home, &metadata->current_backup);
  PathNode *user = findSubnode(home, "user", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(user, &metadata->current_backup);
  PathNode *text_txt = findSubnode(user, "text.txt", BH_not_part_of_repository, BPOL_none, 1, 0);
  mustHaveRegularStat(text_txt, &metadata->current_backup, 2500, NULL, 0);

  PathNode *unneeded = findSubnode(files, "unneeded", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(unneeded, &metadata->current_backup);
  PathNode *directory = findSubnode(unneeded, "directory", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory, &metadata->current_backup);
  PathNode *directory_a = findSubnode(directory, "a", BH_not_part_of_repository, BPOL_none, 1, 4);
  mustHaveDirectoryStat(directory_a, &metadata->current_backup);
  PathNode *directory_b = findSubnode(directory_a, "b", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory_b, &metadata->current_backup);
  PathNode *directory_c = findSubnode(directory_b, "c", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(directory_c, &metadata->backup_history[0], 14,
                       (uint8_t *)"ContentContent??????", 0);
  PathNode *directory_d = findSubnode(directory_a, "d", BH_not_part_of_repository, BPOL_none, 1, 0);
  mustHaveDirectoryStat(directory_d, &metadata->current_backup);
  PathNode *directory_e = findSubnode(directory_a, "e", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(directory_e, &metadata->backup_history[0]);
  PathNode *directory_f = findSubnode(directory_e, "f", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(directory_f, &metadata->backup_history[0], 16,
                       (uint8_t *)"FileFileFileFile????", 0);
  PathNode *directory_g = findSubnode(directory_a, "g", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory_g, &metadata->current_backup);
  PathNode *directory_h = findSubnode(directory_g, "h", BH_not_part_of_repository, BPOL_none, 1, 1);
  mustHaveDirectoryStat(directory_h, &metadata->current_backup);
  PathNode *directory_i = findSubnode(directory_h, "i", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveSymlinkLStat(directory_i, &metadata->backup_history[0], "../../b/c");

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 22);

  /* Clean up after test. */
  removePath("tmp/files/home/user/text.txt");
  removePath("tmp/files/home/user");
  removePath("tmp/files/home");
  removePath("tmp/files/unneeded/directory/a/b");
  removePath("tmp/files/unneeded/directory/a/d");
  removePath("tmp/files/unneeded/directory/a/g/h/i");
  removePath("tmp/files/unneeded/directory/a/g/h");
  removePath("tmp/files/unneeded/directory/a/g");
  removePath("tmp/files/unneeded/directory/a");
  removePath("tmp/files/unneeded/directory");
  removePath("tmp/files/unneeded");
}

/** Generates deeply nested files with varying policies. */
static void runPhase9(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_9_node)
{
  /* Generate various files. */
  makeDir("tmp/files/foo/bar/test");
  makeDir("tmp/files/foo/bar/test/path");
  makeDir("tmp/files/foo/bar/test/path/a");
  makeDir("tmp/files/foo/dir/a");
  makeDir("tmp/files/one");
  makeDir("tmp/files/one/two");
  makeDir("tmp/files/one/two/three");
  makeDir("tmp/files/one/two/three/a");
  makeDir("tmp/files/one/two/three/b");
  makeDir("tmp/files/one/two/three/d");
  makeDir("tmp/files/backup dir");
  makeDir("tmp/files/backup dir/a");
  makeDir("tmp/files/backup dir/a/b");
  makeDir("tmp/files/backup dir/c");
  makeDir("tmp/files/backup dir/c/2");
  makeDir("tmp/files/nano");
  makeDir("tmp/files/nano/a1");
  makeDir("tmp/files/nano/a2");
  makeDir("tmp/files/nano/a3");
  makeDir("tmp/files/nano/a3/1");
  makeDir("tmp/files/nano/a3/1/3");
  makeDir("tmp/files/nb");
  makeDir("tmp/files/nb/manual");
  makeDir("tmp/files/nb/manual/a");
  makeDir("tmp/files/nb/docs");
  makeDir("tmp/files/nb/a");
  makeDir("tmp/files/nb/a/foo");
  makeDir("tmp/files/nb/a/abc");
  makeDir("tmp/files/bin");
  makeDir("tmp/files/bin/a");
  makeDir("tmp/files/bin/a/b");
  makeDir("tmp/files/bin/a/b/c");
  makeDir("tmp/files/bin/a/b/c/2");
  makeDir("tmp/files/bin/1");
  makeDir("tmp/files/bin/1/2");
  makeDir("tmp/files/bin/one");
  makeDir("tmp/files/bin/one/b");
  makeDir("tmp/files/bin/one/c");
  makeDir("tmp/files/bin/one/d");
  makeDir("tmp/files/bin/two");
  makeDir("tmp/files/bin/two/four");
  makeDir("tmp/files/bin/two/four/a");
  makeDir("tmp/files/bin/two/four/a/b");
  makeDir("tmp/files/bin/two/five");
  makeDir("tmp/files/bin/two/five/0");
  makeDir("tmp/files/bin/two/five/0/zero");
  generateFile("tmp/files/foo/dir/a/b", "1232", 2);
  generateFile("tmp/files/foo/dir/a/c", "abcdedcb", 1);
  generateFile("tmp/files/one/two/three/b/c", "Foo", 4);
  generateFile("tmp/files/one/two/three/d/1", "BAR", 5);
  generateFile("tmp/files/backup dir/c/2/3", "Lorem Ipsum", 1);
  generateFile("tmp/files/nano/a1/1", "", 0);
  generateFile("tmp/files/nano/a1/2", "@", 20);
  generateFile("tmp/files/nano/a2/a", "[]", 10);
  generateFile("tmp/files/nano/a3/1/2", "^foo$\n^bar$", 1);
  generateFile("tmp/files/nb/manual/a/123.txt", "-CONTENT-", 1);
  generateFile("tmp/files/nb/manual/b", "m", 21);
  generateFile("tmp/files/nb/docs/1.txt", "m", 21);
  generateFile("tmp/files/nb/a/foo/bar", "q", 20);
  generateFile("tmp/files/nb/a/abc/1", "Hello world\n", 2);
  generateFile("tmp/files/bin/a/b/c/1", "empty\n", 200);
  generateFile("tmp/files/bin/a/b/d", "Large\n", 200);
  generateFile("tmp/files/bin/1/2/3", "nested-file ", 12);
  generateFile("tmp/files/bin/one/a", "This is a test file\n", 20);
  generateFile("tmp/files/bin/one/b/1", "dummy", 1);
  generateFile("tmp/files/bin/one/d/e", "This is a super file\n", 100);
  generateFile("tmp/files/bin/two/four/a/b/c", "#", 19);
  generateFile("tmp/files/bin/two/five/0/zero/null", "", 0);
  makeSymlink("/dev/null",              "tmp/files/one/two/three/d/2");
  makeSymlink("/proc/cpuinfo",          "tmp/files/backup dir/c/1");
  makeSymlink("../../non-existing.txt", "tmp/files/nano/a2/b");
  makeSymlink("../non-existing-dir",    "tmp/files/nb/a/abc/2");
  makeSymlink("/usr/share/doc",         "tmp/files/bin/one/b/2");
  makeSymlink("/root/.vimrc",           "tmp/files/bin/two/three");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 10);
  checkHistPoint(metadata, 0, 0, phase_timestamps[7], cwd_depth + 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_9_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 78);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 84);
  checkHistPoint(metadata, 0, 0, phase_timestamps[7], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 6);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[2]);
  PathNode *bar_test = findSubnode(bar, "test", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(bar_test, &metadata->current_backup);
  PathNode *bar_path = findSubnode(bar_test, "path", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(bar_path, &metadata->current_backup);
  PathNode *bar_path_a = findSubnode(bar_path, "a", BH_added, BPOL_track, 1, 0);
  mustHaveDirectoryCached(bar_path_a, &metadata->current_backup);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[2], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[1]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[2], 0, (uint8_t *)"???", 0);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[2], 84, some_file_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[2], "../some file");
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[2]);
  PathNode *dir_a = findSubnode(dir, "a", BH_added, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir_a, &metadata->current_backup);
  PathNode *dir_b = findSubnode(dir_a, "b", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(dir_b, &metadata->current_backup, 8, NULL, 0);
  PathNode *dir_c = findSubnode(dir_a, "c", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(dir_c, &metadata->current_backup, 8, NULL, 0);

  PathNode *one = findSubnode(files, "one", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryCached(one, &metadata->current_backup);
  PathNode *two = findSubnode(one, "two", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryCached(two, &metadata->current_backup);
  PathNode *three = findSubnode(two, "three", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryCached(three, &metadata->current_backup);
  PathNode *three_a = findSubnode(three, "a", BH_added, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(three_a, &metadata->current_backup);
  PathNode *three_b = findSubnode(three, "b", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(three_b, &metadata->current_backup);
  PathNode *three_c = findSubnode(three_b, "c", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(three_c, &metadata->current_backup, 12, NULL, 0);
  PathNode *three_d = findSubnode(three, "d", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(three_d, &metadata->current_backup);
  PathNode *three_1 = findSubnode(three_d, "1", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(three_1, &metadata->current_backup, 15, NULL, 0);
  PathNode *three_2 = findSubnode(three_d, "2", BH_added, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(three_2, &metadata->current_backup, "/dev/null");

  PathNode *backup_dir = findSubnode(files, "backup dir", BH_added, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir, &metadata->current_backup);
  PathNode *backup_dir_a = findSubnode(backup_dir, "a", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_a, &metadata->current_backup);
  PathNode *backup_dir_b = findSubnode(backup_dir_a, "b", BH_added, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(backup_dir_b, &metadata->current_backup);
  PathNode *backup_dir_c = findSubnode(backup_dir, "c", BH_added, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir_c, &metadata->current_backup);
  PathNode *backup_dir_1 = findSubnode(backup_dir_c, "1", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(backup_dir_1, &metadata->current_backup, "/proc/cpuinfo");
  PathNode *backup_dir_2 = findSubnode(backup_dir_c, "2", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_2, &metadata->current_backup);
  PathNode *backup_dir_3 = findSubnode(backup_dir_2, "3", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(backup_dir_3, &metadata->current_backup, 11, NULL, 0);

  PathNode *nano = findSubnode(files, "nano", BH_added, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(nano, &metadata->current_backup);
  PathNode *nano_a1 = findSubnode(nano, "a1", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(nano_a1, &metadata->current_backup);
  PathNode *nano_a1_1 = findSubnode(nano_a1, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_1, &metadata->current_backup, 0, NULL, 0);
  PathNode *nano_a1_2 = findSubnode(nano_a1, "2", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_2, &metadata->current_backup, 20, NULL, 0);
  PathNode *nano_a2 = findSubnode(nano, "a2", BH_added, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(nano_a2, &metadata->current_backup);
  PathNode *nano_a2_a = findSubnode(nano_a2, "a", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(nano_a2_a, &metadata->current_backup, 20, NULL, 0);
  PathNode *nano_a2_b = findSubnode(nano_a2, "b", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(nano_a2_b, &metadata->current_backup, "../../non-existing.txt");
  PathNode *nano_a3 = findSubnode(nano, "a3", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(nano_a3, &metadata->current_backup);
  PathNode *nano_a3_1 = findSubnode(nano_a3, "1", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(nano_a3_1, &metadata->current_backup);
  PathNode *nano_a3_2 = findSubnode(nano_a3_1, "2", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(nano_a3_2, &metadata->current_backup, 11, NULL, 0);
  PathNode *nano_a3_3 = findSubnode(nano_a3_1, "3", BH_added, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(nano_a3_3, &metadata->current_backup);

  PathNode *nb = findSubnode(files, "nb", BH_added, BPOL_mirror, 1, 3);
  mustHaveDirectoryCached(nb, &metadata->current_backup);
  PathNode *manual = findSubnode(nb, "manual", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(manual, &metadata->current_backup);
  PathNode *manual_a = findSubnode(manual, "a", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(manual_a, &metadata->current_backup);
  PathNode *manual_123_txt = findSubnode(manual_a, "123.txt", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(manual_123_txt, &metadata->current_backup, 9, NULL, 0);
  PathNode *manual_b = findSubnode(manual, "b", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(manual_b, &metadata->current_backup, 21, NULL, 0);
  PathNode *docs = findSubnode(nb, "docs", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(docs, &metadata->current_backup);
  PathNode *docs_1_txt = findSubnode(docs, "1.txt", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(docs_1_txt, &metadata->current_backup, 21, NULL, 0);
  PathNode *nb_a = findSubnode(nb, "a", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(nb_a, &metadata->current_backup);
  PathNode *nb_a_foo = findSubnode(nb_a, "foo", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(nb_a_foo, &metadata->current_backup);
  PathNode *nb_a_bar = findSubnode(nb_a_foo, "bar", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(nb_a_bar, &metadata->current_backup, 20, NULL, 0);
  PathNode *nb_a_abc = findSubnode(nb_a, "abc", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(nb_a_abc, &metadata->current_backup);
  PathNode *nb_a_abc_1 = findSubnode(nb_a_abc, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(nb_a_abc_1, &metadata->current_backup, 24, NULL, 0);
  PathNode *nb_a_abc_2 = findSubnode(nb_a_abc, "2", BH_added, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(nb_a_abc_2, &metadata->current_backup, "../non-existing-dir");

  PathNode *bin = findSubnode(files, "bin", BH_added, BPOL_track, 1, 4);
  mustHaveDirectoryCached(bin, &metadata->current_backup);
  PathNode *bin_a = findSubnode(bin, "a", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_a, &metadata->current_backup);
  PathNode *bin_b = findSubnode(bin_a, "b", BH_added, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(bin_b, &metadata->current_backup);
  PathNode *bin_c = findSubnode(bin_b, "c", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_c, &metadata->current_backup);
  PathNode *bin_c_1 = findSubnode(bin_c, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_c_1, &metadata->current_backup, 1200, NULL, 0);
  PathNode *bin_c_2 = findSubnode(bin_c, "2", BH_added, BPOL_track, 1, 0);
  mustHaveDirectoryCached(bin_c_2, &metadata->current_backup);
  PathNode *bin_d = findSubnode(bin_b, "d", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_d, &metadata->current_backup, 1200, NULL, 0);
  PathNode *bin_1 = findSubnode(bin, "1", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(bin_1, &metadata->current_backup);
  PathNode *bin_2 = findSubnode(bin_1, "2", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(bin_2, &metadata->current_backup);
  PathNode *bin_3 = findSubnode(bin_2, "3", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_3, &metadata->current_backup, 144, NULL, 0);
  PathNode *bin_one = findSubnode(bin, "one", BH_added, BPOL_mirror, 1, 4);
  mustHaveDirectoryCached(bin_one, &metadata->current_backup);
  PathNode *bin_one_a = findSubnode(bin_one, "a", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_a, &metadata->current_backup, 400, NULL, 0);
  PathNode *bin_one_b = findSubnode(bin_one, "b", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_one_b, &metadata->current_backup);
  PathNode *bin_one_1 = findSubnode(bin_one_b, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_one_1, &metadata->current_backup, 5, NULL, 0);
  PathNode *bin_one_2 = findSubnode(bin_one_b, "2", BH_added, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(bin_one_2, &metadata->current_backup, "/usr/share/doc");
  PathNode *bin_one_c = findSubnode(bin_one, "c", BH_added, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(bin_one_c, &metadata->current_backup);
  PathNode *bin_one_d = findSubnode(bin_one, "d", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_one_d, &metadata->current_backup);
  PathNode *bin_one_e = findSubnode(bin_one_d, "e", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_e, &metadata->current_backup, 2100, NULL, 0);
  PathNode *bin_two = findSubnode(bin, "two", BH_added, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bin_two, &metadata->current_backup);
  PathNode *bin_three = findSubnode(bin_two, "three", BH_added, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(bin_three, &metadata->current_backup, "/root/.vimrc");
  PathNode *bin_four = findSubnode(bin_two, "four", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(bin_four, &metadata->current_backup);
  PathNode *bin_four_a = findSubnode(bin_four, "a", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_a, &metadata->current_backup);
  PathNode *bin_four_b = findSubnode(bin_four_a, "b", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_b, &metadata->current_backup);
  PathNode *bin_four_c = findSubnode(bin_four_b, "c", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_four_c, &metadata->current_backup, 19, NULL, 0);
  PathNode *bin_five = findSubnode(bin_two, "five", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(bin_five, &metadata->current_backup);
  PathNode *bin_five_0 = findSubnode(bin_five, "0", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_0, &metadata->current_backup);
  PathNode *bin_five_zero = findSubnode(bin_five_0, "zero", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_zero, &metadata->current_backup);
  PathNode *bin_five_null = findSubnode(bin_five_zero, "null", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_five_null, &metadata->current_backup, 0, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 30);
  mustHaveRegularCached(dir_b,          &metadata->current_backup, 8,    (uint8_t *)"12321232",             0);
  mustHaveRegularCached(dir_c,          &metadata->current_backup, 8,    (uint8_t *)"abcdedcb",             0);
  mustHaveRegularCached(three_c,        &metadata->current_backup, 12,   (uint8_t *)"FooFooFooFoo",         0);
  mustHaveRegularCached(three_1,        &metadata->current_backup, 15,   (uint8_t *)"BARBARBARBARBAR",      0);
  mustHaveRegularCached(backup_dir_3,   &metadata->current_backup, 11,   (uint8_t *)"Lorem Ipsum",          0);
  mustHaveRegularCached(nano_a1_1,      &metadata->current_backup, 0,    (uint8_t *)"%%%%",                 0);
  mustHaveRegularCached(nano_a1_2,      &metadata->current_backup, 20,   (uint8_t *)"@@@@@@@@@@@@@@@@@@@@", 0);
  mustHaveRegularCached(nano_a2_a,      &metadata->current_backup, 20,   (uint8_t *)"[][][][][][][][][][]", 0);
  mustHaveRegularCached(nano_a3_2,      &metadata->current_backup, 11,   (uint8_t *) "^foo$\n^bar$",        0);
  mustHaveRegularCached(manual_123_txt, &metadata->current_backup, 9,    (uint8_t *)"-CONTENT-",            0);
  mustHaveRegularCached(manual_b,       &metadata->current_backup, 21,   nb_manual_b_hash,                  0);
  mustHaveRegularCached(docs_1_txt,     &metadata->current_backup, 21,   nb_manual_b_hash,                  0);
  mustHaveRegularCached(nb_a_bar,       &metadata->current_backup, 20,   (uint8_t *)"qqqqqqqqqqqqqqqqqqqq", 0);
  mustHaveRegularCached(nb_a_abc_1,     &metadata->current_backup, 24,   nb_a_abc_1_hash,                   0);
  mustHaveRegularCached(bin_c_1,        &metadata->current_backup, 1200, bin_c_1_hash,                      0);
  mustHaveRegularCached(bin_d,          &metadata->current_backup, 1200, data_d_hash,                       0);
  mustHaveRegularCached(bin_3,          &metadata->current_backup, 144,  nested_1_hash,                     0);
  mustHaveRegularCached(bin_one_a,      &metadata->current_backup, 400,  three_hash,                        0);
  mustHaveRegularCached(bin_one_1,      &metadata->current_backup, 5,    (uint8_t *)"dummy",                0);
  mustHaveRegularCached(bin_one_e,      &metadata->current_backup, 2100, super_hash,                        0);
  mustHaveRegularCached(bin_four_c,     &metadata->current_backup, 19,   (uint8_t *)"###################",  0);
  mustHaveRegularCached(bin_five_null,  &metadata->current_backup, 0,    (uint8_t *)"???",                  0);
}

/** Removes various files, which are expected to get removed during phase
  10. */
static void phase10RemoveFiles(void)
{
  removePath("tmp/files/bin/two/three");
  removePath("tmp/files/bin/one/b/2");
  removePath("tmp/files/nano/a2/b");
  removePath("tmp/files/backup dir/c/1");
  removePath("tmp/files/bin/two/five/0/zero/null");
  removePath("tmp/files/bin/two/four/a/b/c");
  removePath("tmp/files/bin/one/d/e");
  removePath("tmp/files/bin/one/b/1");
  removePath("tmp/files/bin/one/a");
  removePath("tmp/files/bin/1/2/3");
  removePath("tmp/files/bin/a/b/d");
  removePath("tmp/files/bin/a/b/c/1");
  removePath("tmp/files/nano/a3/1/2");
  removePath("tmp/files/nano/a2/a");
  removePath("tmp/files/nano/a1/2");
  removePath("tmp/files/nano/a1/1");
  removePath("tmp/files/backup dir/c/2/3");
  removePath("tmp/files/foo/dir/a/c");
  removePath("tmp/files/foo/dir/a/b");
  removePath("tmp/files/bin/two/five/0/zero");
  removePath("tmp/files/bin/two/five/0");
  removePath("tmp/files/bin/two/five");
  removePath("tmp/files/bin/two/four/a/b");
  removePath("tmp/files/bin/two/four/a");
  removePath("tmp/files/bin/two/four");
  removePath("tmp/files/bin/two");
  removePath("tmp/files/bin/one/d");
  removePath("tmp/files/bin/one/c");
  removePath("tmp/files/bin/one/b");
  removePath("tmp/files/bin/one");
  removePath("tmp/files/bin/1/2");
  removePath("tmp/files/bin/1");
  removePath("tmp/files/bin/a/b/c/2");
  removePath("tmp/files/bin/a/b/c");
  removePath("tmp/files/bin/a/b");
  removePath("tmp/files/bin/a");
  removePath("tmp/files/bin");
  removePath("tmp/files/nano/a3/1/3");
  removePath("tmp/files/nano/a3/1");
  removePath("tmp/files/nano/a3");
  removePath("tmp/files/nano/a2");
  removePath("tmp/files/nano/a1");
  removePath("tmp/files/nano");
  removePath("tmp/files/backup dir/c/2");
  removePath("tmp/files/backup dir/c");
  removePath("tmp/files/foo/dir/a");
  removePath("tmp/files/foo/bar/test/path/a");
  removePath("tmp/files/foo/bar/test/path");
  removePath("tmp/files/foo/bar/test");
  removePath("tmp/files/foo/dir/empty");
  removePath("tmp/files/foo/dir");
}

/** Removes additional files expected to be removed in phase 10. */
static void phase10RemoveExtraFiles(void)
{
  removePath("tmp/files/one/two/three/d/2");
  removePath("tmp/files/one/two/three/d/1");
  removePath("tmp/files/one/two/three/d");
  removePath("tmp/files/one/two/three/b/c");
  removePath("tmp/files/one/two/three/b");
  removePath("tmp/files/one/two/three/a");
  removePath("tmp/files/one/two/three");
  removePath("tmp/files/one/two");
  removePath("tmp/files/one");
  removePath("tmp/files/backup dir/a/b");
  removePath("tmp/files/backup dir/a");
  removePath("tmp/files/backup dir");
}

/** Tests recursive removing of nested files with varying policies. */
static void runPhase10(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_9_node)
{
  /* Remove various files. */
  phase10RemoveFiles();
  phase10RemoveExtraFiles();
  removePath("tmp/files/nb/a/abc/2");
  removePath("tmp/files/nb/a/abc/1");
  removePath("tmp/files/nb/a/foo/bar");
  removePath("tmp/files/nb/docs/1.txt");
  removePath("tmp/files/nb/manual/b");
  removePath("tmp/files/nb/manual/a/123.txt");
  removePath("tmp/files/nb/a/abc");
  removePath("tmp/files/nb/a/foo");
  removePath("tmp/files/nb/a");
  removePath("tmp/files/nb/docs");
  removePath("tmp/files/nb/manual/a");
  removePath("tmp/files/nb/manual");
  removePath("tmp/files/nb");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 84);
  checkHistPoint(metadata, 0, 0, phase_timestamps[8], cwd_depth + 78);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_9_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 14);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 71);
  checkHistPoint(metadata, 0, 0, phase_timestamps[8], 62);
  checkHistPoint(metadata, 1, 1, phase_timestamps[2], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 6);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[2]);
  PathNode *bar_test = findSubnode(bar, "test", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(bar_test, &metadata->current_backup);
  mustHaveDirectoryCached(bar_test, &metadata->backup_history[0]);
  PathNode *bar_path = findSubnode(bar_test, "path", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(bar_path, &metadata->current_backup);
  mustHaveDirectoryCached(bar_path, &metadata->backup_history[0]);
  PathNode *bar_path_a = findSubnode(bar_path, "a", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(bar_path_a, &metadata->current_backup);
  mustHaveDirectoryCached(bar_path_a, &metadata->backup_history[0]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[2], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[1]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[2], 0, (uint8_t *)"???", 0);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[2], 84, some_file_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_removed, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->backup_history[0]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[2], "../some file");
  PathNode *empty = findSubnode(dir, "empty", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[2]);
  PathNode *dir_a = findSubnode(dir, "a", BH_removed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir_a, &metadata->backup_history[0]);
  PathNode *dir_b = findSubnode(dir_a, "b", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(dir_b, &metadata->backup_history[0], 8, (uint8_t *)"12321232", 0);
  PathNode *dir_c = findSubnode(dir_a, "c", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(dir_c, &metadata->backup_history[0], 8, (uint8_t *)"abcdedcb", 0);

  PathNode *one = findSubnode(files, "one", BH_removed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(one, &metadata->backup_history[0]);
  PathNode *two = findSubnode(one, "two", BH_removed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(two, &metadata->backup_history[0]);
  PathNode *three = findSubnode(two, "three", BH_removed, BPOL_none, 1, 3);
  mustHaveDirectoryCached(three, &metadata->backup_history[0]);
  PathNode *three_a = findSubnode(three, "a", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(three_a, &metadata->backup_history[0]);
  PathNode *three_b = findSubnode(three, "b", BH_removed, BPOL_track, 1, 1);
  mustHaveDirectoryCached(three_b, &metadata->backup_history[0]);
  PathNode *three_c = findSubnode(three_b, "c", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(three_c, &metadata->backup_history[0], 12, (uint8_t *)"FooFooFooFoo", 0);
  PathNode *three_d = findSubnode(three, "d", BH_removed, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(three_d, &metadata->backup_history[0]);
  PathNode *three_1 = findSubnode(three_d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(three_1, &metadata->backup_history[0], 15, (uint8_t *)"BARBARBARBARBAR", 0);
  PathNode *three_2 = findSubnode(three_d, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(three_2, &metadata->backup_history[0], "/dev/null");

  PathNode *backup_dir = findSubnode(files, "backup dir", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir, &metadata->backup_history[0]);
  PathNode *backup_dir_a = findSubnode(backup_dir, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_a, &metadata->backup_history[0]);
  PathNode *backup_dir_b = findSubnode(backup_dir_a, "b", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(backup_dir_b, &metadata->backup_history[0]);
  PathNode *backup_dir_c = findSubnode(backup_dir, "c", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir_c, &metadata->backup_history[0]);
  PathNode *backup_dir_1 = findSubnode(backup_dir_c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(backup_dir_1, &metadata->backup_history[0], "/proc/cpuinfo");
  PathNode *backup_dir_2 = findSubnode(backup_dir_c, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_2, &metadata->backup_history[0]);
  PathNode *backup_dir_3 = findSubnode(backup_dir_2, "3", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(backup_dir_3, &metadata->backup_history[0], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *nano = findSubnode(files, "nano", BH_removed, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(nano, &metadata->backup_history[0]);
  PathNode *nano_a1 = findSubnode(nano, "a1", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(nano_a1, &metadata->backup_history[0]);
  PathNode *nano_a1_1 = findSubnode(nano_a1, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_1, &metadata->backup_history[0], 0, (uint8_t *)"%%%%", 0);
  PathNode *nano_a1_2 = findSubnode(nano_a1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_2, &metadata->backup_history[0], 20, (uint8_t *)"@@@@@@@@@@@@@@@@@@@@", 0);
  PathNode *nano_a2 = findSubnode(nano, "a2", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(nano_a2, &metadata->backup_history[0]);
  PathNode *nano_a2_a = findSubnode(nano_a2, "a", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(nano_a2_a, &metadata->backup_history[0], 20, (uint8_t *)"[][][][][][][][][][]", 0);
  PathNode *nano_a2_b = findSubnode(nano_a2, "b", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(nano_a2_b, &metadata->backup_history[0], "../../non-existing.txt");
  PathNode *nano_a3 = findSubnode(nano, "a3", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(nano_a3, &metadata->backup_history[0]);
  PathNode *nano_a3_1 = findSubnode(nano_a3, "1", BH_removed, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(nano_a3_1, &metadata->backup_history[0]);
  PathNode *nano_a3_2 = findSubnode(nano_a3_1, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(nano_a3_2, &metadata->backup_history[0], 11, (uint8_t *) "^foo$\n^bar$", 0);
  PathNode *nano_a3_3 = findSubnode(nano_a3_1, "3", BH_removed, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(nano_a3_3, &metadata->backup_history[0]);

  PathNode *nb = findSubnode(files, "nb", BH_not_part_of_repository, BPOL_mirror, 1, 3);
  mustHaveDirectoryCached(nb, &metadata->backup_history[0]);
  PathNode *manual = findSubnode(nb, "manual", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryCached(manual, &metadata->backup_history[0]);
  PathNode *manual_a = findSubnode(manual, "a", BH_not_part_of_repository, BPOL_track, 1, 1);
  mustHaveDirectoryCached(manual_a, &metadata->backup_history[0]);
  PathNode *manual_123_txt = findSubnode(manual_a, "123.txt", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(manual_123_txt, &metadata->backup_history[0], 9, (uint8_t *)"-CONTENT-", 0);
  PathNode *manual_b = findSubnode(manual, "b", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(manual_b, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  PathNode *docs = findSubnode(nb, "docs", BH_not_part_of_repository, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(docs, &metadata->backup_history[0]);
  PathNode *docs_1_txt = findSubnode(docs, "1.txt", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(docs_1_txt, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  PathNode *nb_a = findSubnode(nb, "a", BH_not_part_of_repository, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(nb_a, &metadata->backup_history[0]);
  PathNode *nb_a_foo = findSubnode(nb_a, "foo", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(nb_a_foo, &metadata->backup_history[0]);
  PathNode *nb_a_bar = findSubnode(nb_a_foo, "bar", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(nb_a_bar, &metadata->backup_history[0], 20, (uint8_t *)"qqqqqqqqqqqqqqqqqqqq", 0);
  PathNode *nb_a_abc = findSubnode(nb_a, "abc", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryCached(nb_a_abc, &metadata->backup_history[0]);
  PathNode *nb_a_abc_1 = findSubnode(nb_a_abc, "1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(nb_a_abc_1, &metadata->backup_history[0], 24, nb_a_abc_1_hash, 0);
  PathNode *nb_a_abc_2 = findSubnode(nb_a_abc, "2", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(nb_a_abc_2, &metadata->backup_history[0], "../non-existing-dir");

  PathNode *bin = findSubnode(files, "bin", BH_removed, BPOL_track, 2, 4);
  mustHaveNonExisting(bin, &metadata->current_backup);
  mustHaveDirectoryCached(bin, &metadata->backup_history[0]);
  PathNode *bin_a = findSubnode(bin, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_a, &metadata->backup_history[0]);
  PathNode *bin_b = findSubnode(bin_a, "b", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(bin_b, &metadata->backup_history[0]);
  PathNode *bin_c = findSubnode(bin_b, "c", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_c, &metadata->backup_history[0]);
  PathNode *bin_c_1 = findSubnode(bin_c, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_c_1, &metadata->backup_history[0], 1200, bin_c_1_hash, 0);
  PathNode *bin_c_2 = findSubnode(bin_c, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveDirectoryCached(bin_c_2, &metadata->backup_history[0]);
  PathNode *bin_d = findSubnode(bin_b, "d", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_d, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *bin_1 = findSubnode(bin, "1", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_1, &metadata->current_backup);
  mustHaveDirectoryCached(bin_1, &metadata->backup_history[0]);
  PathNode *bin_2 = findSubnode(bin_1, "2", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_2, &metadata->current_backup);
  mustHaveDirectoryCached(bin_2, &metadata->backup_history[0]);
  PathNode *bin_3 = findSubnode(bin_2, "3", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(bin_3, &metadata->current_backup);
  mustHaveRegularCached(bin_3, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *bin_one = findSubnode(bin, "one", BH_removed, BPOL_mirror, 1, 4);
  mustHaveDirectoryCached(bin_one, &metadata->backup_history[0]);
  PathNode *bin_one_a = findSubnode(bin_one, "a", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_a, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *bin_one_b = findSubnode(bin_one, "b", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_one_b, &metadata->backup_history[0]);
  PathNode *bin_one_1 = findSubnode(bin_one_b, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_one_1, &metadata->backup_history[0], 5, (uint8_t *)"dummy", 0);
  PathNode *bin_one_2 = findSubnode(bin_one_b, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(bin_one_2, &metadata->backup_history[0], "/usr/share/doc");
  PathNode *bin_one_c = findSubnode(bin_one, "c", BH_removed, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(bin_one_c, &metadata->backup_history[0]);
  PathNode *bin_one_d = findSubnode(bin_one, "d", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_one_d, &metadata->backup_history[0]);
  PathNode *bin_one_e = findSubnode(bin_one_d, "e", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_e, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *bin_two = findSubnode(bin, "two", BH_removed, BPOL_track, 2, 3);
  mustHaveNonExisting(bin_two, &metadata->current_backup);
  mustHaveDirectoryCached(bin_two, &metadata->backup_history[0]);
  PathNode *bin_three = findSubnode(bin_two, "three", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(bin_three, &metadata->current_backup);
  mustHaveSymlinkLCached(bin_three, &metadata->backup_history[0], "/root/.vimrc");
  PathNode *bin_four = findSubnode(bin_two, "four", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_four, &metadata->current_backup);
  mustHaveDirectoryCached(bin_four, &metadata->backup_history[0]);
  PathNode *bin_four_a = findSubnode(bin_four, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_a, &metadata->backup_history[0]);
  PathNode *bin_four_b = findSubnode(bin_four_a, "b", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_b, &metadata->backup_history[0]);
  PathNode *bin_four_c = findSubnode(bin_four_b, "c", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_four_c, &metadata->backup_history[0], 19, (uint8_t *)"###################", 0);
  PathNode *bin_five = findSubnode(bin_two, "five", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_five, &metadata->current_backup);
  mustHaveDirectoryCached(bin_five, &metadata->backup_history[0]);
  PathNode *bin_five_0 = findSubnode(bin_five, "0", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_0, &metadata->backup_history[0]);
  PathNode *bin_five_zero = findSubnode(bin_five_0, "zero", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_zero, &metadata->backup_history[0]);
  PathNode *bin_five_null = findSubnode(bin_five_zero, "null", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_five_null, &metadata->backup_history[0], 0, (uint8_t *)"???", 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 30);
}

/** Performs a backup with no changes. */
static void runPhase11(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_9_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 71);
  checkHistPoint(metadata, 0, 0, phase_timestamps[9], cwd_depth + 14);
  checkHistPoint(metadata, 1, 1, phase_timestamps[8], 62);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_9_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 3);
  assert_true(metadata->backup_history_length == 4);
  assert_true(metadata->total_path_count == cwd_depth + 71);
  checkHistPoint(metadata, 0, 0, phase_timestamps[9], 11);
  checkHistPoint(metadata, 1, 1, phase_timestamps[8], 62);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 5);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[3]);
  PathNode *bar_test = findSubnode(bar, "test", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bar_test, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bar_test, &metadata->backup_history[1]);
  PathNode *bar_path = findSubnode(bar_test, "path", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bar_path, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bar_path, &metadata->backup_history[1]);
  PathNode *bar_path_a = findSubnode(bar_path, "a", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(bar_path_a, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bar_path_a, &metadata->backup_history[1]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[3], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[2]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[3], 0, (uint8_t *)"???", 0);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[3], 84, some_file_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_removed, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->backup_history[1]);
  PathNode *link = findSubnode(dir, "link", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[3], "../some file");
  PathNode *empty = findSubnode(dir, "empty", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[3]);
  PathNode *dir_a = findSubnode(dir, "a", BH_removed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir_a, &metadata->backup_history[1]);
  PathNode *dir_b = findSubnode(dir_a, "b", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(dir_b, &metadata->backup_history[1], 8, (uint8_t *)"12321232", 0);
  PathNode *dir_c = findSubnode(dir_a, "c", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(dir_c, &metadata->backup_history[1], 8, (uint8_t *)"abcdedcb", 0);

  PathNode *one = findSubnode(files, "one", BH_removed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(one, &metadata->backup_history[1]);
  PathNode *two = findSubnode(one, "two", BH_removed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(two, &metadata->backup_history[1]);
  PathNode *three = findSubnode(two, "three", BH_removed, BPOL_none, 1, 3);
  mustHaveDirectoryCached(three, &metadata->backup_history[1]);
  PathNode *three_a = findSubnode(three, "a", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(three_a, &metadata->backup_history[1]);
  PathNode *three_b = findSubnode(three, "b", BH_removed, BPOL_track, 1, 1);
  mustHaveDirectoryCached(three_b, &metadata->backup_history[1]);
  PathNode *three_c = findSubnode(three_b, "c", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(three_c, &metadata->backup_history[1], 12, (uint8_t *)"FooFooFooFoo", 0);
  PathNode *three_d = findSubnode(three, "d", BH_removed, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(three_d, &metadata->backup_history[1]);
  PathNode *three_1 = findSubnode(three_d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(three_1, &metadata->backup_history[1], 15, (uint8_t *)"BARBARBARBARBAR", 0);
  PathNode *three_2 = findSubnode(three_d, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(three_2, &metadata->backup_history[1], "/dev/null");

  PathNode *backup_dir = findSubnode(files, "backup dir", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir, &metadata->backup_history[1]);
  PathNode *backup_dir_a = findSubnode(backup_dir, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_a, &metadata->backup_history[1]);
  PathNode *backup_dir_b = findSubnode(backup_dir_a, "b", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(backup_dir_b, &metadata->backup_history[1]);
  PathNode *backup_dir_c = findSubnode(backup_dir, "c", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir_c, &metadata->backup_history[1]);
  PathNode *backup_dir_1 = findSubnode(backup_dir_c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(backup_dir_1, &metadata->backup_history[1], "/proc/cpuinfo");
  PathNode *backup_dir_2 = findSubnode(backup_dir_c, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_2, &metadata->backup_history[1]);
  PathNode *backup_dir_3 = findSubnode(backup_dir_2, "3", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(backup_dir_3, &metadata->backup_history[1], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *nano = findSubnode(files, "nano", BH_removed, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(nano, &metadata->backup_history[1]);
  PathNode *nano_a1 = findSubnode(nano, "a1", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(nano_a1, &metadata->backup_history[1]);
  PathNode *nano_a1_1 = findSubnode(nano_a1, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_1, &metadata->backup_history[1], 0, (uint8_t *)"%%%%", 0);
  PathNode *nano_a1_2 = findSubnode(nano_a1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_2, &metadata->backup_history[1], 20, (uint8_t *)"@@@@@@@@@@@@@@@@@@@@", 0);
  PathNode *nano_a2 = findSubnode(nano, "a2", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(nano_a2, &metadata->backup_history[1]);
  PathNode *nano_a2_a = findSubnode(nano_a2, "a", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(nano_a2_a, &metadata->backup_history[1], 20, (uint8_t *)"[][][][][][][][][][]", 0);
  PathNode *nano_a2_b = findSubnode(nano_a2, "b", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(nano_a2_b, &metadata->backup_history[1], "../../non-existing.txt");
  PathNode *nano_a3 = findSubnode(nano, "a3", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(nano_a3, &metadata->backup_history[1]);
  PathNode *nano_a3_1 = findSubnode(nano_a3, "1", BH_removed, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(nano_a3_1, &metadata->backup_history[1]);
  PathNode *nano_a3_2 = findSubnode(nano_a3_1, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(nano_a3_2, &metadata->backup_history[1], 11, (uint8_t *) "^foo$\n^bar$", 0);
  PathNode *nano_a3_3 = findSubnode(nano_a3_1, "3", BH_removed, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(nano_a3_3, &metadata->backup_history[1]);

  PathNode *bin = findSubnode(files, "bin", BH_unchanged, BPOL_track, 2, 4);
  mustHaveNonExisting(bin, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin, &metadata->backup_history[1]);
  PathNode *bin_a = findSubnode(bin, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_a, &metadata->backup_history[1]);
  PathNode *bin_b = findSubnode(bin_a, "b", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(bin_b, &metadata->backup_history[1]);
  PathNode *bin_c = findSubnode(bin_b, "c", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_c, &metadata->backup_history[1]);
  PathNode *bin_c_1 = findSubnode(bin_c, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_c_1, &metadata->backup_history[1], 1200, bin_c_1_hash, 0);
  PathNode *bin_c_2 = findSubnode(bin_c, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveDirectoryCached(bin_c_2, &metadata->backup_history[1]);
  PathNode *bin_d = findSubnode(bin_b, "d", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_d, &metadata->backup_history[1], 1200, data_d_hash, 0);
  PathNode *bin_1 = findSubnode(bin, "1", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_1, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_1, &metadata->backup_history[1]);
  PathNode *bin_2 = findSubnode(bin_1, "2", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_2, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_2, &metadata->backup_history[1]);
  PathNode *bin_3 = findSubnode(bin_2, "3", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(bin_3, &metadata->backup_history[0]);
  mustHaveRegularCached(bin_3, &metadata->backup_history[1], 144, nested_1_hash, 0);
  PathNode *bin_one = findSubnode(bin, "one", BH_removed, BPOL_mirror, 1, 4);
  mustHaveDirectoryCached(bin_one, &metadata->backup_history[1]);
  PathNode *bin_one_a = findSubnode(bin_one, "a", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_a, &metadata->backup_history[1], 400, three_hash, 0);
  PathNode *bin_one_b = findSubnode(bin_one, "b", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_one_b, &metadata->backup_history[1]);
  PathNode *bin_one_1 = findSubnode(bin_one_b, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_one_1, &metadata->backup_history[1], 5, (uint8_t *)"dummy", 0);
  PathNode *bin_one_2 = findSubnode(bin_one_b, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(bin_one_2, &metadata->backup_history[1], "/usr/share/doc");
  PathNode *bin_one_c = findSubnode(bin_one, "c", BH_removed, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(bin_one_c, &metadata->backup_history[1]);
  PathNode *bin_one_d = findSubnode(bin_one, "d", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_one_d, &metadata->backup_history[1]);
  PathNode *bin_one_e = findSubnode(bin_one_d, "e", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_e, &metadata->backup_history[1], 2100, super_hash, 0);
  PathNode *bin_two = findSubnode(bin, "two", BH_unchanged, BPOL_track, 2, 3);
  mustHaveNonExisting(bin_two, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_two, &metadata->backup_history[1]);
  PathNode *bin_three = findSubnode(bin_two, "three", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(bin_three, &metadata->backup_history[0]);
  mustHaveSymlinkLCached(bin_three, &metadata->backup_history[1], "/root/.vimrc");
  PathNode *bin_four = findSubnode(bin_two, "four", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_four, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_four, &metadata->backup_history[1]);
  PathNode *bin_four_a = findSubnode(bin_four, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_a, &metadata->backup_history[1]);
  PathNode *bin_four_b = findSubnode(bin_four_a, "b", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_b, &metadata->backup_history[1]);
  PathNode *bin_four_c = findSubnode(bin_four_b, "c", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_four_c, &metadata->backup_history[1], 19, (uint8_t *)"###################", 0);
  PathNode *bin_five = findSubnode(bin_two, "five", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_five, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_five, &metadata->backup_history[1]);
  PathNode *bin_five_0 = findSubnode(bin_five, "0", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_0, &metadata->backup_history[1]);
  PathNode *bin_five_zero = findSubnode(bin_five_0, "zero", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_zero, &metadata->backup_history[1]);
  PathNode *bin_five_null = findSubnode(bin_five_zero, "null", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_five_null, &metadata->backup_history[1], 0, (uint8_t *)"???", 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 30);
}

/** Performs a backup after restoring files removed in phase 10. */
static void runPhase12(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_9_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 71);
  checkHistPoint(metadata, 0, 0, phase_timestamps[10], cwd_depth + 3);
  checkHistPoint(metadata, 1, 1, phase_timestamps[9], 11);
  checkHistPoint(metadata, 2, 2, phase_timestamps[8], 62);
  checkHistPoint(metadata, 3, 3, phase_timestamps[2], 1);
  checkHistPoint(metadata, 4, 4, phase_timestamps[0], 6);

  restoreWithTimeRecursively(metadata->paths);
  initiateBackup(metadata, phase_9_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 20);
  assert_true(metadata->backup_history_length == 5);
  assert_true(metadata->total_path_count == cwd_depth + 71);
  checkHistPoint(metadata, 0, 0, phase_timestamps[10], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[9], 11);
  checkHistPoint(metadata, 2, 2, phase_timestamps[8], 57);
  checkHistPoint(metadata, 3, 3, phase_timestamps[2], 1);
  checkHistPoint(metadata, 4, 4, phase_timestamps[0], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 5);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[4]);
  PathNode *bar_test = findSubnode(bar, "test", BH_added, BPOL_track, 3, 1);
  mustHaveDirectoryCached(bar_test, &metadata->current_backup);
  mustHaveNonExisting(bar_test, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bar_test, &metadata->backup_history[2]);
  PathNode *bar_path = findSubnode(bar_test, "path", BH_added, BPOL_track, 3, 1);
  mustHaveDirectoryCached(bar_path, &metadata->current_backup);
  mustHaveNonExisting(bar_path, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bar_path, &metadata->backup_history[2]);
  PathNode *bar_path_a = findSubnode(bar_path, "a", BH_added, BPOL_track, 3, 0);
  mustHaveDirectoryCached(bar_path_a, &metadata->current_backup);
  mustHaveNonExisting(bar_path_a, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bar_path_a, &metadata->backup_history[2]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[4], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_added, BPOL_track, 3, 0);
  mustHaveRegularCached(two_txt, &metadata->current_backup, 0, (uint8_t *)"???", 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[3]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[4], 0, (uint8_t *)"???", 0);

  PathNode *some_file = findSubnode(foo, "some file", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[4], 84, some_file_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->current_backup);
  PathNode *link = findSubnode(dir, "link", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[4], "../some file");
  PathNode *empty = findSubnode(dir, "empty", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[4]);
  PathNode *dir_a = findSubnode(dir, "a", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir_a, &metadata->current_backup);
  PathNode *dir_b = findSubnode(dir_a, "b", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(dir_b, &metadata->backup_history[2], 8, (uint8_t *)"12321232", 0);
  PathNode *dir_c = findSubnode(dir_a, "c", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(dir_c, &metadata->backup_history[2], 8, (uint8_t *)"abcdedcb", 0);

  PathNode *one = findSubnode(files, "one", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryCached(one, &metadata->current_backup);
  PathNode *two = findSubnode(one, "two", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryCached(two, &metadata->current_backup);
  PathNode *three = findSubnode(two, "three", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryCached(three, &metadata->current_backup);
  PathNode *three_a = findSubnode(three, "a", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(three_a, &metadata->backup_history[2]);
  PathNode *three_b = findSubnode(three, "b", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryCached(three_b, &metadata->backup_history[2]);
  PathNode *three_c = findSubnode(three_b, "c", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(three_c, &metadata->backup_history[2], 12, (uint8_t *)"FooFooFooFoo", 0);
  PathNode *three_d = findSubnode(three, "d", BH_unchanged, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(three_d, &metadata->backup_history[2]);
  PathNode *three_1 = findSubnode(three_d, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(three_1, &metadata->backup_history[2], 15, (uint8_t *)"BARBARBARBARBAR", 0);
  PathNode *three_2 = findSubnode(three_d, "2", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(three_2, &metadata->backup_history[2], "/dev/null");

  PathNode *backup_dir = findSubnode(files, "backup dir", BH_unchanged, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir, &metadata->backup_history[2]);
  PathNode *backup_dir_a = findSubnode(backup_dir, "a", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_a, &metadata->backup_history[2]);
  PathNode *backup_dir_b = findSubnode(backup_dir_a, "b", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(backup_dir_b, &metadata->backup_history[2]);
  PathNode *backup_dir_c = findSubnode(backup_dir, "c", BH_unchanged, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir_c, &metadata->backup_history[2]);
  PathNode *backup_dir_1 = findSubnode(backup_dir_c, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(backup_dir_1, &metadata->backup_history[2], "/proc/cpuinfo");
  PathNode *backup_dir_2 = findSubnode(backup_dir_c, "2", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_2, &metadata->backup_history[2]);
  PathNode *backup_dir_3 = findSubnode(backup_dir_2, "3", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(backup_dir_3, &metadata->backup_history[2], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *nano = findSubnode(files, "nano", BH_unchanged, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(nano, &metadata->backup_history[2]);
  PathNode *nano_a1 = findSubnode(nano, "a1", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(nano_a1, &metadata->backup_history[2]);
  PathNode *nano_a1_1 = findSubnode(nano_a1, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_1, &metadata->backup_history[2], 0, (uint8_t *)"%%%%", 0);
  PathNode *nano_a1_2 = findSubnode(nano_a1, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_2, &metadata->backup_history[2], 20, (uint8_t *)"@@@@@@@@@@@@@@@@@@@@", 0);
  PathNode *nano_a2 = findSubnode(nano, "a2", BH_unchanged, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(nano_a2, &metadata->backup_history[2]);
  PathNode *nano_a2_a = findSubnode(nano_a2, "a", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(nano_a2_a, &metadata->backup_history[2], 20, (uint8_t *)"[][][][][][][][][][]", 0);
  PathNode *nano_a2_b = findSubnode(nano_a2, "b", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(nano_a2_b, &metadata->backup_history[2], "../../non-existing.txt");
  PathNode *nano_a3 = findSubnode(nano, "a3", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(nano_a3, &metadata->backup_history[2]);
  PathNode *nano_a3_1 = findSubnode(nano_a3, "1", BH_unchanged, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(nano_a3_1, &metadata->backup_history[2]);
  PathNode *nano_a3_2 = findSubnode(nano_a3_1, "2", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(nano_a3_2, &metadata->backup_history[2], 11, (uint8_t *) "^foo$\n^bar$", 0);
  PathNode *nano_a3_3 = findSubnode(nano_a3_1, "3", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(nano_a3_3, &metadata->backup_history[2]);

  PathNode *bin = findSubnode(files, "bin", BH_added, BPOL_track, 3, 4);
  mustHaveDirectoryCached(bin, &metadata->current_backup);
  mustHaveNonExisting(bin, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bin, &metadata->backup_history[2]);
  PathNode *bin_a = findSubnode(bin, "a", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_a, &metadata->backup_history[2]);
  PathNode *bin_b = findSubnode(bin_a, "b", BH_unchanged, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(bin_b, &metadata->backup_history[2]);
  PathNode *bin_c = findSubnode(bin_b, "c", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_c, &metadata->backup_history[2]);
  PathNode *bin_c_1 = findSubnode(bin_c, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_c_1, &metadata->backup_history[2], 1200, bin_c_1_hash, 0);
  PathNode *bin_c_2 = findSubnode(bin_c, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveDirectoryCached(bin_c_2, &metadata->backup_history[2]);
  PathNode *bin_d = findSubnode(bin_b, "d", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_d, &metadata->backup_history[2], 1200, data_d_hash, 0);
  PathNode *bin_1 = findSubnode(bin, "1", BH_added, BPOL_track, 3, 1);
  mustHaveDirectoryCached(bin_1, &metadata->current_backup);
  mustHaveNonExisting(bin_1, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bin_1, &metadata->backup_history[2]);
  PathNode *bin_2 = findSubnode(bin_1, "2", BH_added, BPOL_track, 3, 1);
  mustHaveDirectoryCached(bin_2, &metadata->current_backup);
  mustHaveNonExisting(bin_2, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bin_2, &metadata->backup_history[2]);
  PathNode *bin_3 = findSubnode(bin_2, "3", BH_added, BPOL_track, 3, 0);
  mustHaveRegularCached(bin_3, &metadata->current_backup, 144, NULL, 0);
  mustHaveNonExisting(bin_3, &metadata->backup_history[1]);
  mustHaveRegularCached(bin_3, &metadata->backup_history[2], 144, nested_1_hash, 0);
  PathNode *bin_one = findSubnode(bin, "one", BH_unchanged, BPOL_mirror, 1, 4);
  mustHaveDirectoryCached(bin_one, &metadata->backup_history[2]);
  PathNode *bin_one_a = findSubnode(bin_one, "a", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_a, &metadata->backup_history[2], 400, three_hash, 0);
  PathNode *bin_one_b = findSubnode(bin_one, "b", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_one_b, &metadata->backup_history[2]);
  PathNode *bin_one_1 = findSubnode(bin_one_b, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_one_1, &metadata->backup_history[2], 5, (uint8_t *)"dummy", 0);
  PathNode *bin_one_2 = findSubnode(bin_one_b, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(bin_one_2, &metadata->backup_history[2], "/usr/share/doc");
  PathNode *bin_one_c = findSubnode(bin_one, "c", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(bin_one_c, &metadata->backup_history[2]);
  PathNode *bin_one_d = findSubnode(bin_one, "d", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_one_d, &metadata->backup_history[2]);
  PathNode *bin_one_e = findSubnode(bin_one_d, "e", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_e, &metadata->backup_history[2], 2100, super_hash, 0);
  PathNode *bin_two = findSubnode(bin, "two", BH_added, BPOL_track, 3, 3);
  mustHaveDirectoryCached(bin_two, &metadata->current_backup);
  mustHaveNonExisting(bin_two, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bin_two, &metadata->backup_history[2]);
  PathNode *bin_three = findSubnode(bin_two, "three", BH_added, BPOL_track, 3, 0);
  mustHaveSymlinkLStat(bin_three, &metadata->current_backup, "/root/.vimrc");
  mustHaveNonExisting(bin_three, &metadata->backup_history[1]);
  mustHaveSymlinkLCached(bin_three, &metadata->backup_history[2], "/root/.vimrc");
  PathNode *bin_four = findSubnode(bin_two, "four", BH_added, BPOL_track, 3, 1);
  mustHaveDirectoryCached(bin_four, &metadata->current_backup);
  mustHaveNonExisting(bin_four, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bin_four, &metadata->backup_history[2]);
  PathNode *bin_four_a = findSubnode(bin_four, "a", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_a, &metadata->backup_history[2]);
  PathNode *bin_four_b = findSubnode(bin_four_a, "b", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_b, &metadata->backup_history[2]);
  PathNode *bin_four_c = findSubnode(bin_four_b, "c", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_four_c, &metadata->backup_history[2], 19, (uint8_t *)"###################", 0);
  PathNode *bin_five = findSubnode(bin_two, "five", BH_added, BPOL_track, 3, 1);
  mustHaveDirectoryCached(bin_five, &metadata->current_backup);
  mustHaveNonExisting(bin_five, &metadata->backup_history[1]);
  mustHaveDirectoryCached(bin_five, &metadata->backup_history[2]);
  PathNode *bin_five_0 = findSubnode(bin_five, "0", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_0, &metadata->backup_history[2]);
  PathNode *bin_five_zero = findSubnode(bin_five_0, "zero", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_zero, &metadata->backup_history[2]);
  PathNode *bin_five_null = findSubnode(bin_five_zero, "null", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_five_null, &metadata->backup_history[2], 0, (uint8_t *)"???", 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 30);
  mustHaveRegularCached(bin_3, &metadata->current_backup, 144, nested_1_hash, 0);
}

/** Like phase 12, but restores only a few files and uses a different
  search tree. */
static void runPhase13(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_13_node)
{
  /* Remove various files. */
  removePath("tmp/files/foo/dir/link");
  removePath("tmp/files/foo/bar/2.txt");
  phase10RemoveFiles();

  /* Generate various files. */
  generateFile("tmp/files/bin", "0", 2123);

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 71);
  checkHistPoint(metadata, 0, 0, phase_timestamps[9], cwd_depth + 14);
  checkHistPoint(metadata, 1, 1, phase_timestamps[8], 62);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 6);
  initiateBackup(metadata, phase_13_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 7);
  assert_true(metadata->backup_history_length == 4);
  assert_true(metadata->total_path_count == cwd_depth + 43);
  checkHistPoint(metadata, 0, 0, phase_timestamps[9], 8);
  checkHistPoint(metadata, 1, 1, phase_timestamps[8], 34);
  checkHistPoint(metadata, 2, 2, phase_timestamps[2], 1);
  checkHistPoint(metadata, 3, 3, phase_timestamps[0], 3);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 5);
  PathNode *foo = findSubnode(files, "foo", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(foo, &metadata->current_backup);

  PathNode *bar = findSubnode(foo, "bar", BH_unchanged, BPOL_track, 1, 3);
  mustHaveDirectoryCached(bar, &metadata->backup_history[3]);
  PathNode *bar_test = findSubnode(bar, "test", BH_not_part_of_repository, BPOL_track, 2, 1);
  mustHaveNonExisting(bar_test, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bar_test, &metadata->backup_history[1]);
  PathNode *bar_path = findSubnode(bar_test, "path", BH_not_part_of_repository, BPOL_track, 2, 1);
  mustHaveNonExisting(bar_path, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bar_path, &metadata->backup_history[1]);
  PathNode *bar_path_a = findSubnode(bar_path, "a", BH_not_part_of_repository, BPOL_track, 2, 0);
  mustHaveNonExisting(bar_path_a, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bar_path_a, &metadata->backup_history[1]);
  PathNode *one_txt = findSubnode(bar, "1.txt", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(one_txt, &metadata->backup_history[3], 12, (uint8_t *)"A small file", 0);
  PathNode *two_txt = findSubnode(bar, "2.txt", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(two_txt, &metadata->backup_history[2]);
  mustHaveRegularCached(two_txt, &metadata->backup_history[3], 0, (uint8_t *)"???", 0);

  PathNode *some_file = findSubnode(foo, "some file", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularStat(some_file, &metadata->backup_history[3], 84, some_file_hash, 0);

  PathNode *dir = findSubnode(foo, "dir", BH_not_part_of_repository, BPOL_none, 1, 3);
  mustHaveDirectoryCached(dir, &metadata->backup_history[1]);
  PathNode *link = findSubnode(dir, "link", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(link, &metadata->backup_history[3], "../some file");
  PathNode *empty = findSubnode(dir, "empty", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(empty, &metadata->backup_history[3]);
  PathNode *dir_a = findSubnode(dir, "a", BH_not_part_of_repository, BPOL_none, 1, 2);
  mustHaveDirectoryCached(dir_a, &metadata->backup_history[1]);
  PathNode *dir_b = findSubnode(dir_a, "b", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(dir_b, &metadata->backup_history[1], 8, (uint8_t *)"12321232", 0);
  PathNode *dir_c = findSubnode(dir_a, "c", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(dir_c, &metadata->backup_history[1], 8, (uint8_t *)"abcdedcb", 0);

  PathNode *one = findSubnode(files, "one", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryCached(one, &metadata->current_backup);
  PathNode *two = findSubnode(one, "two", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryCached(two, &metadata->current_backup);
  PathNode *three = findSubnode(two, "three", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryCached(three, &metadata->current_backup);
  PathNode *three_a = findSubnode(three, "a", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(three_a, &metadata->backup_history[1]);
  PathNode *three_b = findSubnode(three, "b", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryCached(three_b, &metadata->backup_history[1]);
  PathNode *three_c = findSubnode(three_b, "c", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(three_c, &metadata->backup_history[1], 12, (uint8_t *)"FooFooFooFoo", 0);
  PathNode *three_d = findSubnode(three, "d", BH_not_part_of_repository, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(three_d, &metadata->backup_history[1]);
  PathNode *three_1 = findSubnode(three_d, "1", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(three_1, &metadata->backup_history[1], 15, (uint8_t *)"BARBARBARBARBAR", 0);
  PathNode *three_2 = findSubnode(three_d, "2", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(three_2, &metadata->backup_history[1], "/dev/null");

  PathNode *backup_dir = findSubnode(files, "backup dir", BH_unchanged, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir, &metadata->backup_history[1]);
  PathNode *backup_dir_a = findSubnode(backup_dir, "a", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_a, &metadata->backup_history[1]);
  PathNode *backup_dir_b = findSubnode(backup_dir_a, "b", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(backup_dir_b, &metadata->backup_history[1]);
  PathNode *backup_dir_c = findSubnode(backup_dir, "c", BH_not_part_of_repository, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(backup_dir_c, &metadata->backup_history[1]);
  PathNode *backup_dir_1 = findSubnode(backup_dir_c, "1", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(backup_dir_1, &metadata->backup_history[1], "/proc/cpuinfo");
  PathNode *backup_dir_2 = findSubnode(backup_dir_c, "2", BH_not_part_of_repository, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(backup_dir_2, &metadata->backup_history[1]);
  PathNode *backup_dir_3 = findSubnode(backup_dir_2, "3", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(backup_dir_3, &metadata->backup_history[1], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *nano = findSubnode(files, "nano", BH_not_part_of_repository, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(nano, &metadata->backup_history[1]);
  PathNode *nano_a1 = findSubnode(nano, "a1", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryCached(nano_a1, &metadata->backup_history[1]);
  PathNode *nano_a1_1 = findSubnode(nano_a1, "1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_1, &metadata->backup_history[1], 0, (uint8_t *)"%%%%", 0);
  PathNode *nano_a1_2 = findSubnode(nano_a1, "2", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(nano_a1_2, &metadata->backup_history[1], 20, (uint8_t *)"@@@@@@@@@@@@@@@@@@@@", 0);
  PathNode *nano_a2 = findSubnode(nano, "a2", BH_not_part_of_repository, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(nano_a2, &metadata->backup_history[1]);
  PathNode *nano_a2_a = findSubnode(nano_a2, "a", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(nano_a2_a, &metadata->backup_history[1], 20, (uint8_t *)"[][][][][][][][][][]", 0);
  PathNode *nano_a2_b = findSubnode(nano_a2, "b", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(nano_a2_b, &metadata->backup_history[1], "../../non-existing.txt");
  PathNode *nano_a3 = findSubnode(nano, "a3", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(nano_a3, &metadata->backup_history[1]);
  PathNode *nano_a3_1 = findSubnode(nano_a3, "1", BH_not_part_of_repository, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(nano_a3_1, &metadata->backup_history[1]);
  PathNode *nano_a3_2 = findSubnode(nano_a3_1, "2", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(nano_a3_2, &metadata->backup_history[1], 11, (uint8_t *) "^foo$\n^bar$", 0);
  PathNode *nano_a3_3 = findSubnode(nano_a3_1, "3", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(nano_a3_3, &metadata->backup_history[1]);

  PathNode *bin = findSubnode(files, "bin", BH_added, BPOL_track, 3, 4);
  mustHaveRegularStat(bin, &metadata->current_backup, 2123, NULL, 0);
  mustHaveNonExisting(bin, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin, &metadata->backup_history[1]);
  PathNode *bin_a = findSubnode(bin, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_a, &metadata->backup_history[1]);
  PathNode *bin_b = findSubnode(bin_a, "b", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(bin_b, &metadata->backup_history[1]);
  PathNode *bin_c = findSubnode(bin_b, "c", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_c, &metadata->backup_history[1]);
  PathNode *bin_c_1 = findSubnode(bin_c, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_c_1, &metadata->backup_history[1], 1200, bin_c_1_hash, 0);
  PathNode *bin_c_2 = findSubnode(bin_c, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveDirectoryCached(bin_c_2, &metadata->backup_history[1]);
  PathNode *bin_d = findSubnode(bin_b, "d", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_d, &metadata->backup_history[1], 1200, data_d_hash, 0);
  PathNode *bin_1 = findSubnode(bin, "1", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_1, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_1, &metadata->backup_history[1]);
  PathNode *bin_2 = findSubnode(bin_1, "2", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_2, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_2, &metadata->backup_history[1]);
  PathNode *bin_3 = findSubnode(bin_2, "3", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(bin_3, &metadata->backup_history[0]);
  mustHaveRegularCached(bin_3, &metadata->backup_history[1], 144, nested_1_hash, 0);
  PathNode *bin_one = findSubnode(bin, "one", BH_removed, BPOL_mirror, 1, 4);
  mustHaveDirectoryCached(bin_one, &metadata->backup_history[1]);
  PathNode *bin_one_a = findSubnode(bin_one, "a", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_a, &metadata->backup_history[1], 400, three_hash, 0);
  PathNode *bin_one_b = findSubnode(bin_one, "b", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_one_b, &metadata->backup_history[1]);
  PathNode *bin_one_1 = findSubnode(bin_one_b, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_one_1, &metadata->backup_history[1], 5, (uint8_t *)"dummy", 0);
  PathNode *bin_one_2 = findSubnode(bin_one_b, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(bin_one_2, &metadata->backup_history[1], "/usr/share/doc");
  PathNode *bin_one_c = findSubnode(bin_one, "c", BH_removed, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(bin_one_c, &metadata->backup_history[1]);
  PathNode *bin_one_d = findSubnode(bin_one, "d", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_one_d, &metadata->backup_history[1]);
  PathNode *bin_one_e = findSubnode(bin_one_d, "e", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_e, &metadata->backup_history[1], 2100, super_hash, 0);
  PathNode *bin_two = findSubnode(bin, "two", BH_unchanged, BPOL_track, 2, 3);
  mustHaveNonExisting(bin_two, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_two, &metadata->backup_history[1]);
  PathNode *bin_three = findSubnode(bin_two, "three", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(bin_three, &metadata->backup_history[0]);
  mustHaveSymlinkLCached(bin_three, &metadata->backup_history[1], "/root/.vimrc");
  PathNode *bin_four = findSubnode(bin_two, "four", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_four, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_four, &metadata->backup_history[1]);
  PathNode *bin_four_a = findSubnode(bin_four, "a", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_a, &metadata->backup_history[1]);
  PathNode *bin_four_b = findSubnode(bin_four_a, "b", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(bin_four_b, &metadata->backup_history[1]);
  PathNode *bin_four_c = findSubnode(bin_four_b, "c", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(bin_four_c, &metadata->backup_history[1], 19, (uint8_t *)"###################", 0);
  PathNode *bin_five = findSubnode(bin_two, "five", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(bin_five, &metadata->backup_history[0]);
  mustHaveDirectoryCached(bin_five, &metadata->backup_history[1]);
  PathNode *bin_five_0 = findSubnode(bin_five, "0", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_0, &metadata->backup_history[1]);
  PathNode *bin_five_zero = findSubnode(bin_five_0, "zero", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_five_zero, &metadata->backup_history[1]);
  PathNode *bin_five_null = findSubnode(bin_five_zero, "null", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_five_null, &metadata->backup_history[1], 0, (uint8_t *)"???", 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 33);
  mustHaveRegularStat(bin, &metadata->current_backup, 2123, bin_hash, 0);
}

/** Creates and backups various simple files with the copy policy. */
static void runPhase14(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_14_node)
{
  /* Generate various files. */
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/c");
  makeDir("tmp/files/d");
  makeDir("tmp/files/d/3");
  generateFile("tmp/files/a",   "This file is a", 1);
  generateFile("tmp/files/d/1", "This file is 1", 1);
  makeSymlink("/dev/null",      "tmp/files/b");
  makeSymlink("invalid target", "tmp/files/d/2");

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, phase_14_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 9);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 9);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 4);
  PathNode *a = findSubnode(files, "a", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(a, &metadata->current_backup, 14, NULL, 0);
  PathNode *b = findSubnode(files, "b", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(b, &metadata->current_backup, "/dev/null");
  PathNode *c = findSubnode(files, "c", BH_added, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(c, &metadata->current_backup);
  PathNode *d = findSubnode(files, "d", BH_added, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(d, &metadata->current_backup);
  PathNode *d_1 = findSubnode(d, "1", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(d_1, &metadata->current_backup, 14, NULL, 0);
  PathNode *d_2 = findSubnode(d, "2", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(d_2, &metadata->current_backup, "invalid target");
  PathNode *d_3 = findSubnode(d, "3", BH_added, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(d_3, &metadata->current_backup);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
  mustHaveRegularCached(a,   &metadata->current_backup, 14, (uint8_t *)"This file is a", 0);
  mustHaveRegularCached(d_1, &metadata->current_backup, 14, (uint8_t *)"This file is 1", 0);
}

/** Removes various files which are expected to be removed in phase 15. */
static void phase15RemoveFiles(void)
{
  removePath("tmp/files/d/3");
  removePath("tmp/files/d/2");
  removePath("tmp/files/d/1");
  removePath("tmp/files/c");
  removePath("tmp/files/b");
  removePath("tmp/files/a");
}

/** Removes some files generated in phase 14 and performs a backup. */
static void runPhase15(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_14_node)
{
  /* Remove various files. */
  phase15RemoveFiles();

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 9);
  checkHistPoint(metadata, 0, 0, phase_timestamps[13], cwd_depth + 9);
  initiateBackup(metadata, phase_14_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 9);
  checkHistPoint(metadata, 0, 0, phase_timestamps[13], 7);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);
  PathNode *a = findSubnode(files, "a", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(a, &metadata->backup_history[0], 14, (uint8_t *)"This file is a", 0);
  PathNode *b = findSubnode(files, "b", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(b, &metadata->backup_history[0], "/dev/null");
  PathNode *c = findSubnode(files, "c", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(c, &metadata->backup_history[0]);
  PathNode *d = findSubnode(files, "d", BH_unchanged, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(d, &metadata->backup_history[0]);
  PathNode *d_1 = findSubnode(d, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(d_1, &metadata->backup_history[0], 14, (uint8_t *)"This file is 1", 0);
  PathNode *d_2 = findSubnode(d, "2", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(d_2, &metadata->backup_history[0], "invalid target");
  PathNode *d_3 = findSubnode(d, "3", BH_removed, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(d_3, &metadata->backup_history[0]);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Restores all files previously deleted and checks the result. */
static void runPhase16(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_14_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 9);
  checkHistPoint(metadata, 0, 0, phase_timestamps[14], cwd_depth + 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[13], 7);

  restoreWithTimeRecursively(metadata->paths);
  initiateBackup(metadata, phase_14_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 9);
  checkHistPoint(metadata, 0, 0, phase_timestamps[14], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[13], 7);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);
  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(a, &metadata->backup_history[1], 14, (uint8_t *)"This file is a", 0);
  PathNode *b = findSubnode(files, "b", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(b, &metadata->backup_history[1], "/dev/null");
  PathNode *c = findSubnode(files, "c", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(c, &metadata->backup_history[1]);
  PathNode *d = findSubnode(files, "d", BH_unchanged, BPOL_copy, 1, 3);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);
  PathNode *d_1 = findSubnode(d, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(d_1, &metadata->backup_history[1], 14, (uint8_t *)"This file is 1", 0);
  PathNode *d_2 = findSubnode(d, "2", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(d_2, &metadata->backup_history[1], "invalid target");
  PathNode *d_3 = findSubnode(d, "3", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryCached(d_3, &metadata->backup_history[1]);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Asserts that the given node contains a "dummy" subnode with the
  specified properties. The hash can be NULL. */
static void mustHaveDummy(PathNode *node, BackupHint hint,
                          BackupPolicy policy, Backup *backup,
                          const char *hash)
{
  PathNode *dummy = findSubnode(node, "dummy", hint, policy, 1, 0);
  mustHaveRegularStat(dummy, backup, 5, (const uint8_t *)hash, 0);
}

/** Creates various dummy files for testing change detection in nodes
  without a policy. */
static void runPhase17(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_17_node)
{
  /* Generate various files. */
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/a/b");
  makeDir("tmp/files/a/c");
  makeDir("tmp/files/d");
  makeDir("tmp/files/d/e");
  makeDir("tmp/files/d/f");
  makeDir("tmp/files/g");
  makeDir("tmp/files/h");
  generateFile("tmp/files/a/b/dummy", "dummy", 1);
  generateFile("tmp/files/a/c/dummy", "dummy", 1);
  generateFile("tmp/files/d/e/dummy", "dummy", 1);
  generateFile("tmp/files/d/f/dummy", "dummy", 1);
  generateFile("tmp/files/g/dummy",   "dummy", 1);
  generateFile("tmp/files/h/dummy",   "dummy", 1);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, phase_17_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 16);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 16);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 4);

  PathNode *a = findSubnode(files, "a", BH_added, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_added, BPOL_copy, &metadata->current_backup, NULL);
  PathNode *c = findSubnode(a, "c", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_added, BPOL_track, &metadata->current_backup, NULL);

  PathNode *d = findSubnode(files, "d", BH_added, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_added, BPOL_mirror, &metadata->current_backup, NULL);
  PathNode *f = findSubnode(d, "f", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_added, BPOL_track, &metadata->current_backup, NULL);

  PathNode *g = findSubnode(files, "g", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_added, BPOL_track, &metadata->current_backup, NULL);

  PathNode *h = findSubnode(files, "h", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_added, BPOL_copy, &metadata->current_backup, NULL);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
  mustHaveDummy(b, BH_added, BPOL_copy,   &metadata->current_backup, "dummy");
  mustHaveDummy(c, BH_added, BPOL_track,  &metadata->current_backup, "dummy");
  mustHaveDummy(e, BH_added, BPOL_mirror, &metadata->current_backup, "dummy");
  mustHaveDummy(f, BH_added, BPOL_track,  &metadata->current_backup, "dummy");
  mustHaveDummy(g, BH_added, BPOL_track,  &metadata->current_backup, "dummy");
  mustHaveDummy(h, BH_added, BPOL_copy,   &metadata->current_backup, "dummy");
}

/** Modifies the current metadata in such a way, that a subsequent
  initiation will find changes in nodes without a policy. */
static void runPhase18(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_17_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps[16], cwd_depth + 16);
  initiateBackup(metadata, phase_17_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 10);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps[16], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);

  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_unchanged, BPOL_copy, &metadata->backup_history[0], "dummy");
  PathNode *c = findSubnode(a, "c", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_unchanged, BPOL_track, &metadata->backup_history[0], "dummy");

  PathNode *d = findSubnode(files, "d", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_unchanged, BPOL_mirror, &metadata->backup_history[0], "dummy");
  PathNode *f = findSubnode(d, "f", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_unchanged, BPOL_track, &metadata->backup_history[0], "dummy");

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_unchanged, BPOL_track, &metadata->backup_history[0], "dummy");

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_unchanged, BPOL_copy, &metadata->backup_history[0], "dummy");

  /* Modify various path nodes. */
  a->history->state.uid++;
  b->history->state.gid++;
  c->history->state.metadata.dir.mode++;
  d->history->state.metadata.dir.timestamp++;

  e->history->state.uid++;
  e->history->state.metadata.dir.mode++;

  f->history->state.gid++;
  f->history->state.metadata.dir.timestamp++;

  g->history->state.metadata.dir.mode++;
  g->history->state.metadata.dir.timestamp++;

  h->history->state.gid++;
  h->history->state.metadata.dir.mode++;
  h->history->state.metadata.dir.timestamp++;

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Tests detection of changes in nodes without a policy. */
static void runPhase19(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_17_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps[17], cwd_depth + 10);
  checkHistPoint(metadata, 1, 1, phase_timestamps[16], 6);
  initiateBackup(metadata, phase_17_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 10);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps[17], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[16], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);

  PathNode *a = findSubnode(files, "a", BH_owner_changed, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_owner_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");
  PathNode *c = findSubnode(a, "c", BH_permissions_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *d = findSubnode(files, "d", BH_timestamp_changed, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_owner_changed | BH_permissions_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_unchanged, BPOL_mirror, &metadata->backup_history[1], "dummy");
  PathNode *f = findSubnode(d, "f", BH_owner_changed | BH_timestamp_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *g = findSubnode(files, "g", BH_permissions_changed | BH_timestamp_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *h = findSubnode(files, "h", BH_owner_changed | BH_permissions_changed | BH_timestamp_changed,
                            BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Tests metadata written by phase 19. */
static void runPhase20(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_17_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps[18], cwd_depth + 10);
  checkHistPoint(metadata, 1, 1, phase_timestamps[16], 6);
  initiateBackup(metadata, phase_17_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 10);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps[18], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[16], 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 4);

  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");
  PathNode *c = findSubnode(a, "c", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *d = findSubnode(files, "d", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_unchanged, BPOL_mirror, &metadata->backup_history[1], "dummy");
  PathNode *f = findSubnode(d, "f", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Prepares files and metadata for testing detection of changes in files.

  @param cwd_path The path to the current working directory.
  @param cwd_depth The recursion depth of the current working directory.
  @param change_detection_node The search tree to use for preparing the
  test.
  @param policy The policy to test.
*/
static void initChangeDetectionTest(String cwd_path, size_t cwd_depth,
                                    SearchNode *change_detection_node,
                                    BackupPolicy policy)
{
  /* Prepare test and create various files. */
  assertTmpIsCleared();
  makeDir("tmp/files/0");
  makeDir("tmp/files/0/1");
  makeDir("tmp/files/2");
  makeDir("tmp/files/3");
  makeDir("tmp/files/4");
  makeDir("tmp/files/5");
  makeDir("tmp/files/8");
  makeDir("tmp/files/13");
  makeDir("tmp/files/14");
  makeSymlink("/dev/non-existing",    "tmp/files/5/6");
  makeSymlink("uid changing symlink", "tmp/files/15");
  makeSymlink("gid changing symlink", "tmp/files/16");
  makeSymlink("symlink content",      "tmp/files/17");
  makeSymlink("symlink content",      "tmp/files/18");
  makeSymlink("gid + content",        "tmp/files/19");
  makeSymlink("content, uid, gid",    "tmp/files/20");
  generateFile("tmp/files/5/7",  "This is a test file\n",  20);
  generateFile("tmp/files/8/9",  "This is a file\n",       1);
  generateFile("tmp/files/8/10", "GID and UID",            1);
  generateFile("tmp/files/8/11", "",                       0);
  generateFile("tmp/files/8/12", "nano-backup ",           7);
  generateFile("tmp/files/21",   "This is a super file\n", 100);
  generateFile("tmp/files/22",   "Large\n",                200);
  generateFile("tmp/files/23",   "nested-file ",           12);
  generateFile("tmp/files/24",   "nested ",                8);
  generateFile("tmp/files/25",   "a/b/c/",                 7);
  generateFile("tmp/files/26",   "Hello world\n",          2);
  generateFile("tmp/files/27",   "m",                      21);
  generateFile("tmp/files/28",   "0",                      2123);
  generateFile("tmp/files/29",   "empty\n",                200);
  generateFile("tmp/files/30",   "This is a test file\n",  20);
  generateFile("tmp/files/31",   "This is a super file\n", 100);
  generateFile("tmp/files/32",   "A small file",           1);
  generateFile("tmp/files/33",   "Another file",           1);
  generateFile("tmp/files/34",   "Some dummy text",        1);
  generateFile("tmp/files/35",   "abcdefghijkl",           1);
  generateFile("tmp/files/36",   "Nano Backup",            1);
  generateFile("tmp/files/37",   "nested ",                8);
  generateFile("tmp/files/38",   "",                       0);
  generateFile("tmp/files/39",   "",                       0);
  generateFile("tmp/files/40",   "",                       0);
  generateFile("tmp/files/41",   "random file",            1);
  generateFile("tmp/files/42",   "",                       0);
  generateFile("tmp/files/43",   "Large\n",                200);
  generateFile("tmp/files/44",   "nested-file ",           12);
  generateFile("tmp/files/45",   "Small file",             1);
  generateFile("tmp/files/46",   "Test file",              1);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 49);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 49);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_added, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->current_backup);
  PathNode *node_1 = findSubnode(node_0, "1", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->current_backup);
  PathNode *node_2 = findSubnode(files, "2", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->current_backup);
  PathNode *node_3 = findSubnode(files, "3", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  PathNode *node_4 = findSubnode(files, "4", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  PathNode *node_5 = findSubnode(files, "5", BH_added, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->current_backup);
  PathNode *node_6 = findSubnode(node_5, "6", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "/dev/non-existing");
  PathNode *node_7 = findSubnode(node_5, "7", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, NULL, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_added, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->current_backup);
  PathNode *node_9 = findSubnode(node_8, "9", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 15, NULL, 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_12 = findSubnode(node_8, "12", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 84, NULL, 0);
  PathNode *node_13 = findSubnode(files, "13", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->current_backup);
  PathNode *node_14 = findSubnode(files, "14", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->current_backup);
  PathNode *node_15 = findSubnode(files, "15", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->current_backup, "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->current_backup, "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->current_backup, "symlink content");
  PathNode *node_18 = findSubnode(files, "18", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->current_backup, "symlink content");
  PathNode *node_19 = findSubnode(files, "19", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->current_backup, "gid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->current_backup, "content, uid, gid");
  PathNode *node_21 = findSubnode(files, "21", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, NULL, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, NULL, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 56, NULL, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->current_backup, 42, NULL, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 24, NULL, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, NULL, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2123, NULL, 0);
  PathNode *node_29 = findSubnode(files, "29", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_30 = findSubnode(files, "30", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, NULL, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, NULL, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_32, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_33 = findSubnode(files, "33", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_34 = findSubnode(files, "34", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, NULL, 0);
  PathNode *node_35 = findSubnode(files, "35", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_36 = findSubnode(files, "36", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_37 = findSubnode(files, "37", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 56, NULL, 0);
  PathNode *node_38 = findSubnode(files, "38", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_38, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_39 = findSubnode(files, "39", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_40 = findSubnode(files, "40", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_41 = findSubnode(files, "41", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_42 = findSubnode(files, "42", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_42, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_43 = findSubnode(files, "43", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_44 = findSubnode(files, "44", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 144, NULL, 0);
  PathNode *node_45 = findSubnode(files, "45", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_45, &metadata->current_backup, 10, NULL, 0);
  PathNode *node_46 = findSubnode(files, "46", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_46, &metadata->current_backup, 9, NULL, 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 33);
  mustHaveRegularStat(node_7,  &metadata->current_backup, 400,  three_hash,                    0);
  mustHaveRegularStat(node_9,  &metadata->current_backup, 15,   (uint8_t *)"This is a file\n", 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11,   (uint8_t *)"GID and UID",      0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0,    (uint8_t *)"",                 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 84,   some_file_hash,                0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash,                    0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash,                   0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144,  nested_1_hash,                 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 56,   nested_2_hash,                 0);
  mustHaveRegularStat(node_25, &metadata->current_backup, 42,   test_c_hash,                   0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 24,   nb_a_abc_1_hash,               0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21,   nb_manual_b_hash,              0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2123, bin_hash,                      0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, bin_c_1_hash,                  0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400,  three_hash,                    0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash,                    0);
  mustHaveRegularStat(node_32, &metadata->current_backup, 12,   (uint8_t *)"A small file",     0);
  mustHaveRegularStat(node_33, &metadata->current_backup, 12,   (uint8_t *)"Another file",     0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15,   (uint8_t *)"Some dummy text",  0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 12,   (uint8_t *)"abcdefghijkl",     0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11,   (uint8_t *)"Nano Backup",      0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 56,   nested_2_hash,                 0);
  mustHaveRegularStat(node_38, &metadata->current_backup, 0,    (uint8_t *)"",                 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0,    (uint8_t *)"",                 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0,    (uint8_t *)"",                 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 11,   (uint8_t *)"random file",      0);
  mustHaveRegularStat(node_42, &metadata->current_backup, 0,    (uint8_t *)"",                 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 1200, data_d_hash,                   0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 144,  nested_1_hash,                 0);
  mustHaveRegularStat(node_45, &metadata->current_backup, 10,   (uint8_t *)"Small file",       0);
  mustHaveRegularStat(node_46, &metadata->current_backup, 9,    (uint8_t *)"Test file",        0);
}

/** Modifies the current metadata in such a way, that a subsequent
  initiation will find changes in nodes. */
static void modifyChangeDetectionTest(String cwd_path, size_t cwd_depth,
                                      SearchNode *change_detection_node,
                                      BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 49);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 47);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->backup_history[0]);
  PathNode *node_1 = findSubnode(node_0, "1", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->backup_history[0]);
  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->backup_history[0]);
  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0]);
  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0]);
  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->backup_history[0]);
  PathNode *node_6 = findSubnode(node_5, "6", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0], "/dev/non-existing");
  PathNode *node_7 = findSubnode(node_5, "7", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->backup_history[0]);
  PathNode *node_9 = findSubnode(node_8, "9", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->backup_history[0], 15, (uint8_t *)"This is a file\n", 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->backup_history[0], 11, (uint8_t *)"GID and UID", 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_12 = findSubnode(node_8, "12", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->backup_history[0], 84, some_file_hash, 0);
  PathNode *node_13 = findSubnode(files, "13", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->backup_history[0]);
  PathNode *node_14 = findSubnode(files, "14", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->backup_history[0]);
  PathNode *node_15 = findSubnode(files, "15", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->backup_history[0], "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->backup_history[0], "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[0], "symlink content");
  PathNode *node_18 = findSubnode(files, "18", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[0], "symlink content");
  PathNode *node_19 = findSubnode(files, "19", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->backup_history[0], "gid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->backup_history[0], "content, uid, gid");
  PathNode *node_21 = findSubnode(files, "21", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[0], 56, nested_2_hash, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[0], 42, test_c_hash, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->backup_history[0], 24, nb_a_abc_1_hash, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->backup_history[0], 2123, bin_hash, 0);
  PathNode *node_29 = findSubnode(files, "29", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->backup_history[0], 1200, bin_c_1_hash, 0);
  PathNode *node_30 = findSubnode(files, "30", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[0], 12, (uint8_t *)"A small file", 0);
  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[0], 12, (uint8_t *)"Another file", 0);
  PathNode *node_34 = findSubnode(files, "34", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->backup_history[0], 15, (uint8_t *)"Some dummy text", 0);
  PathNode *node_35 = findSubnode(files, "35", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->backup_history[0], 12, (uint8_t *)"abcdefghijkl", 0);
  PathNode *node_36 = findSubnode(files, "36", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->backup_history[0], 11, (uint8_t *)"Nano Backup", 0);
  PathNode *node_37 = findSubnode(files, "37", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[0], 56, nested_2_hash, 0);
  PathNode *node_38 = findSubnode(files, "38", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_39 = findSubnode(files, "39", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_40 = findSubnode(files, "40", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_41 = findSubnode(files, "41", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->backup_history[0], 11, (uint8_t *)"random file", 0);
  PathNode *node_42 = findSubnode(files, "42", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_42, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_43 = findSubnode(files, "43", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *node_44 = findSubnode(files, "44", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *node_45 = findSubnode(files, "45", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[0], 10, (uint8_t *)"Small file", 0);
  PathNode *node_46 = findSubnode(files, "46", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_46, &metadata->backup_history[0], 9, (uint8_t *)"Test file", 0);

  /* Modify various path nodes. */
  node_0->history->state.uid++;
  node_1->history->state.gid++;
  node_2->history->state.metadata.dir.mode++;
  node_3->history->state.metadata.dir.timestamp++;
  node_4->history->state.metadata.dir.mode++;
  node_4->history->state.metadata.dir.timestamp++;
  node_5->history->state.uid++;
  node_5->history->state.metadata.dir.mode++;

  remakeSymlink("/dev/null", "tmp/files/5/6");
  node_6->history->state.uid++;

  node_7->history->state.uid++;
  node_8->history->state.gid++;
  node_8->history->state.metadata.dir.timestamp++;

  regenerateFile(node_9, "This is test", 1);
  node_9->history->state.uid++;

  node_10->history->state.metadata.reg.timestamp++;
  node_11->history->state.uid++;
  node_11->history->state.metadata.reg.mode++;

  regenerateFile(node_12, "a short string", 1);
  node_12->history->state.gid++;
  node_12->history->state.metadata.reg.mode++;

  node_13->history->state.gid++;
  node_13->history->state.metadata.dir.mode++;
  node_13->history->state.metadata.dir.timestamp++;
  node_14->history->state.uid++;
  node_14->history->state.metadata.dir.timestamp++;
  node_15->history->state.uid++;
  node_16->history->state.gid++;
  remakeSymlink("symlink-content",        "tmp/files/17");
  remakeSymlink("symlink content string", "tmp/files/18");

  remakeSymlink("uid + content", "tmp/files/19");
  node_19->history->state.gid++;

  remakeSymlink("content, uid, gid ", "tmp/files/20");
  node_20->history->state.uid++;
  node_20->history->state.gid++;

  node_21->history->state.gid++;
  node_22->history->state.metadata.reg.mode++;
  node_23->history->state.metadata.reg.timestamp++;
  regenerateFile(node_24, "nested ", 9);
  regenerateFile(node_25, "a/B/c/",  7);

  regenerateFile(node_26, "Hello world", 2);
  node_26->history->state.gid++;

  regenerateFile(node_27, "M", 21);
  node_27->history->state.metadata.reg.mode++;

  regenerateFile(node_28, "0", 2124);
  node_28->history->state.metadata.reg.timestamp++;

  regenerateFile(node_29, "Empty\n", 200);
  node_29->history->state.uid++;
  node_29->history->state.metadata.reg.timestamp++;

  node_30->history->state.uid++;
  node_30->history->state.metadata.reg.mode++;
  node_30->history->state.metadata.reg.timestamp++;
  node_31->history->state.uid++;
  node_31->history->state.gid++;
  regenerateFile(node_32, "A small file.", 1);
  regenerateFile(node_33, "another file", 1);

  regenerateFile(node_34, "some dummy text", 1);
  node_34->history->state.metadata.reg.timestamp++;

  regenerateFile(node_35, "?", 1);
  node_35->history->state.metadata.reg.mode++;

  regenerateFile(node_36, "nano backup", 1);
  node_36->history->state.gid++;
  node_36->history->state.metadata.reg.mode++;

  regenerateFile(node_37, "",  0);
  regenerateFile(node_38, "@", 1);
  node_39->history->state.gid++;
  node_40->history->state.metadata.reg.timestamp++;

  regenerateFile(node_41, "", 0);
  node_41->history->state.metadata.reg.mode++;

  regenerateFile(node_42, "Backup\n", 74);
  node_42->history->state.gid++;

  regenerateFile(node_43, "Large\n", 2);
  node_43->history->state.metadata.reg.timestamp++;

  regenerateFile(node_44, "Q", 20);
  regenerateFile(node_45, "q", 21);

  regenerateFile(node_46, "test\n", 123);
  node_46->history->state.uid++;

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 33);
}

/** Tests the changes injected by modifyChangeDetectionTest(). */
static void changeDetectionTest(String cwd_path, size_t cwd_depth,
                                SearchNode *change_detection_node,
                                BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 47);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 47);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 2);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_owner_changed, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->current_backup);
  PathNode *node_1 = findSubnode(node_0, "1", BH_owner_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->current_backup);
  PathNode *node_2 = findSubnode(files, "2", BH_permissions_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->current_backup);
  PathNode *node_3 = findSubnode(files, "3", BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  PathNode *node_4 = findSubnode(files, "4", BH_permissions_changed | BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  PathNode *node_5 = findSubnode(files, "5", BH_owner_changed | BH_permissions_changed, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->current_backup);
  PathNode *node_6 = findSubnode(node_5, "6", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "/dev/null");
  PathNode *node_7 = findSubnode(node_5, "7", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, three_hash, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_owner_changed | BH_timestamp_changed, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->current_backup);
  PathNode *node_9 = findSubnode(node_8, "9", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 12, (uint8_t *)"This is a file\n", 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, (uint8_t *)"GID and UID", 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_owner_changed | BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, (uint8_t *)"", 0);
  PathNode *node_12 = findSubnode(node_8, "12", BH_owner_changed | BH_permissions_changed |
                                  BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 14,  some_file_hash, 0);
  PathNode *node_13 = findSubnode(files, "13", BH_owner_changed | BH_permissions_changed |
                                  BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->current_backup);
  PathNode *node_14 = findSubnode(files, "14", BH_owner_changed | BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->current_backup);
  PathNode *node_15 = findSubnode(files, "15", BH_owner_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->current_backup, "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_owner_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->current_backup, "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->current_backup, "symlink-content");
  PathNode *node_18 = findSubnode(files, "18", BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->current_backup, "symlink content string");
  PathNode *node_19 = findSubnode(files, "19", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->current_backup, "uid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->current_backup, "content, uid, gid ");
  PathNode *node_21 = findSubnode(files, "21", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, nested_1_hash, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 63, nested_2_hash, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 22, nb_a_abc_1_hash, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, nb_manual_b_hash, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_timestamp_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2124, bin_hash, 0);
  PathNode *node_29 = findSubnode(files, "29", BH_owner_changed | BH_timestamp_changed |
                                  BH_content_changed | BH_fresh_hash, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, node_29_hash, 0);
  PathNode *node_30 = findSubnode(files, "30", BH_owner_changed | BH_permissions_changed |
                                  BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, three_hash, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_content_changed, policy, 1, 0);
  node_32->history->state.metadata.reg.hash[12] = '?';
  mustHaveRegularStat(node_32, &metadata->current_backup, 13, (uint8_t *)"A small file??", 0);
  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);
  PathNode *node_34 = findSubnode(files, "34", BH_timestamp_changed | BH_content_changed |
                                  BH_fresh_hash, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, (uint8_t *)"some dummy text", 0);
  PathNode *node_35 = findSubnode(files, "35", BH_permissions_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 1, (uint8_t *)"abcdefghijkl", 0);
  PathNode *node_36 = findSubnode(files, "36", BH_owner_changed | BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, (uint8_t *)"Nano Backup", 0);
  PathNode *node_37 = findSubnode(files, "37", BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 0, nested_2_hash, 0);
  PathNode *node_38 = findSubnode(files, "38", BH_content_changed, policy, 1, 0);
  node_38->history->state.metadata.reg.hash[0] = 'P';
  mustHaveRegularStat(node_38, &metadata->current_backup, 1, (uint8_t *)"PPP", 0);
  PathNode *node_39 = findSubnode(files, "39", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, (uint8_t *)"", 0);
  PathNode *node_40 = findSubnode(files, "40", BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, (uint8_t *)"", 0);
  PathNode *node_41 = findSubnode(files, "41", BH_permissions_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 0, (uint8_t *)"random file", 0);
  PathNode *node_42 = findSubnode(files, "42", BH_owner_changed | BH_content_changed, policy, 1, 0);
  memset(node_42->history->state.metadata.reg.hash, 'X', FILE_HASH_SIZE);
  node_42->history->state.metadata.reg.slot = 52;
  mustHaveRegularStat(node_42, &metadata->current_backup, 518, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 52);
  PathNode *node_43 = findSubnode(files, "43", BH_timestamp_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 12, data_d_hash, 0);
  PathNode *node_44 = findSubnode(files, "44", BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 20, nested_1_hash, 0);
  PathNode *node_45 = findSubnode(files, "45", BH_content_changed, policy, 1, 0);
  memset(&node_45->history->state.metadata.reg.hash[10], 'J', 10);
  node_45->history->state.metadata.reg.slot = 149;
  mustHaveRegularStat(node_45, &metadata->current_backup, 21, (uint8_t *)"Small fileJJJJJJJJJJ", 149);
  PathNode *node_46 = findSubnode(files, "46", BH_owner_changed | BH_content_changed, policy, 1, 0);
  memset(&node_46->history->state.metadata.reg.hash[9], '=', 11);
  node_46->history->state.metadata.reg.slot = 2;
  mustHaveRegularStat(node_46, &metadata->current_backup, 615, (uint8_t *)"Test file===========", 2);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 49);
  mustHaveRegularStat(node_7,  &metadata->current_backup,    400,  three_hash,                        0);
  mustHaveRegularStat(node_9,  &metadata->current_backup,    12,   (uint8_t *)"This is test",         0);
  mustHaveRegularStat(node_10, &metadata->current_backup,    11,   (uint8_t *)"GID and UID",          0);
  mustHaveRegularStat(node_11, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_12, &metadata->current_backup,    14,   (uint8_t *)"a short string",       0);
  mustHaveRegularStat(node_21, &metadata->current_backup,    2100, super_hash,                        0);
  mustHaveRegularStat(node_22, &metadata->current_backup,    1200, data_d_hash,                       0);
  mustHaveRegularStat(node_23, &metadata->current_backup,    144,  nested_1_hash,                     0);
  mustHaveRegularStat(node_24, &metadata->current_backup,    63,   node_24_hash,                      0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42,   test_c_hash,                       0);
  mustHaveRegularStat(node_26, &metadata->current_backup,    22,   node_26_hash,                      0);
  mustHaveRegularStat(node_27, &metadata->current_backup,    21,   nb_manual_b_hash,                  0);
  mustHaveRegularStat(node_28, &metadata->current_backup,    2124, node_28_hash,                      0);
  mustHaveRegularStat(node_29, &metadata->current_backup,    1200, node_29_hash,                      0);
  mustHaveRegularStat(node_30, &metadata->current_backup,    400,  three_hash,                        0);
  mustHaveRegularStat(node_31, &metadata->current_backup,    2100, super_hash,                        0);
  mustHaveRegularStat(node_32, &metadata->current_backup,    13,   (uint8_t *)"A small file.",        0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12,   (uint8_t *)"Another file",         0);
  mustHaveRegularStat(node_34, &metadata->current_backup,    15,   (uint8_t *)"some dummy text",      0);
  mustHaveRegularStat(node_35, &metadata->current_backup,    1,    (uint8_t *)"?",                    0);
  mustHaveRegularStat(node_36, &metadata->current_backup,    11,   (uint8_t *)"Nano Backup",          0);
  mustHaveRegularStat(node_37, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_38, &metadata->current_backup,    1,    (uint8_t *)"@",                    0);
  mustHaveRegularStat(node_39, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_40, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_41, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_42, &metadata->current_backup,    518,  node_42_hash,                      0);
  mustHaveRegularStat(node_43, &metadata->current_backup,    12,   (uint8_t *)"Large\nLarge\n",       0);
  mustHaveRegularStat(node_44, &metadata->current_backup,    20,   (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  mustHaveRegularStat(node_45, &metadata->current_backup,    21,   node_45_hash,                      0);
  mustHaveRegularStat(node_46, &metadata->current_backup,    615,  node_46_hash,                      0);
}

/** Tests the metadata written by changeDetectionTest() and cleans up the
  test directory. */
static void postDetectionTest(String cwd_path, size_t cwd_depth,
                              SearchNode *change_detection_node,
                              BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 47);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 3], 2);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 45);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 3], 2);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->backup_history[0]);
  PathNode *node_1 = findSubnode(node_0, "1", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->backup_history[0]);
  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->backup_history[0]);
  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0]);
  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0]);
  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->backup_history[0]);
  PathNode *node_6 = findSubnode(node_5, "6", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0], "/dev/null");
  PathNode *node_7 = findSubnode(node_5, "7", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->backup_history[0]);
  PathNode *node_9 = findSubnode(node_8, "9", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->backup_history[0], 12, (uint8_t *)"This is test", 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->backup_history[0], 11, (uint8_t *)"GID and UID", 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_12 = findSubnode(node_8, "12", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->backup_history[0], 14, (uint8_t *)"a short string", 0);
  PathNode *node_13 = findSubnode(files, "13", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->backup_history[0]);
  PathNode *node_14 = findSubnode(files, "14", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->backup_history[0]);
  PathNode *node_15 = findSubnode(files, "15", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->backup_history[0], "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->backup_history[0], "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[0], "symlink-content");
  PathNode *node_18 = findSubnode(files, "18", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[0], "symlink content string");
  PathNode *node_19 = findSubnode(files, "19", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->backup_history[0], "uid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->backup_history[0], "content, uid, gid ");
  PathNode *node_21 = findSubnode(files, "21", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[0], 63, node_24_hash, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->backup_history[0], 22, node_26_hash, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->backup_history[0], 2124, node_28_hash, 0);
  PathNode *node_29 = findSubnode(files, "29", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->backup_history[0], 1200, node_29_hash, 0);
  PathNode *node_30 = findSubnode(files, "30", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[0], 13, (uint8_t *)"A small file.", 0);
  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);
  PathNode *node_34 = findSubnode(files, "34", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->backup_history[0], 15, (uint8_t *)"some dummy text", 0);
  PathNode *node_35 = findSubnode(files, "35", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->backup_history[0], 1, (uint8_t *)"?", 0);
  PathNode *node_36 = findSubnode(files, "36", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->backup_history[0], 11, (uint8_t *)"Nano Backup", 0);
  PathNode *node_37 = findSubnode(files, "37", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_38 = findSubnode(files, "38", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[0], 1, (uint8_t *)"@", 0);
  PathNode *node_39 = findSubnode(files, "39", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_40 = findSubnode(files, "40", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_41 = findSubnode(files, "41", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_42 = findSubnode(files, "42", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_42, &metadata->backup_history[0], 518, node_42_hash, 0);
  PathNode *node_43 = findSubnode(files, "43", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->backup_history[0], 12, (uint8_t *)"Large\nLarge\n", 0);
  PathNode *node_44 = findSubnode(files, "44", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[0], 20, (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  PathNode *node_45 = findSubnode(files, "45", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[0], 21, node_45_hash, 0);
  PathNode *node_46 = findSubnode(files, "46", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_46, &metadata->backup_history[0], 615, node_46_hash, 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 49);
}

/** Tests change detection in tracked nodes. */
static void trackChangeDetectionTest(String cwd_path, size_t cwd_depth,
                                     SearchNode *track_detection_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[29], cwd_depth + 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[28], 47);
  initiateBackup(metadata, track_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 47);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[29], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[28], 47);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_owner_changed, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_0, &metadata->current_backup);
  struct stat node_0_stats = sStat(node_0->path.str);
  node_0_stats.st_uid++;
  mustHaveDirectoryStats(node_0, &metadata->backup_history[1], node_0_stats);

  PathNode *node_1 = findSubnode(node_0, "1", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_1, &metadata->current_backup);
  struct stat node_1_stats = sStat(node_1->path.str);
  node_1_stats.st_gid++;
  mustHaveDirectoryStats(node_1, &metadata->backup_history[1], node_1_stats);

  PathNode *node_2 = findSubnode(files, "2", BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_2, &metadata->current_backup);
  struct stat node_2_stats = sStat(node_2->path.str);
  node_2_stats.st_mode++;
  mustHaveDirectoryStats(node_2, &metadata->backup_history[1], node_2_stats);

  PathNode *node_3 = findSubnode(files, "3", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  struct stat node_3_stats = sStat(node_3->path.str);
  node_3_stats.st_mtime++;
  mustHaveDirectoryStats(node_3, &metadata->backup_history[1], node_3_stats);

  PathNode *node_4 = findSubnode(files, "4", BH_permissions_changed | BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  struct stat node_4_stats = sStat(node_4->path.str);
  node_4_stats.st_mode++;
  node_4_stats.st_mtime++;
  mustHaveDirectoryStats(node_4, &metadata->backup_history[1], node_4_stats);

  PathNode *node_5 = findSubnode(files, "5", BH_owner_changed | BH_permissions_changed, BPOL_track, 2, 2);
  mustHaveDirectoryStat(node_5, &metadata->current_backup);
  struct stat node_5_stats = sStat(node_5->path.str);
  node_5_stats.st_uid++;
  node_5_stats.st_mode++;
  mustHaveDirectoryStats(node_5, &metadata->backup_history[1], node_5_stats);

  PathNode *node_6 = findSubnode(node_5, "6", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "/dev/null");
  struct stat node_6_stats = sLStat(node_6->path.str);
  node_6_stats.st_uid++;
  mustHaveSymlinkStats(node_6, &metadata->backup_history[1], node_6_stats, "/dev/non-existing");

  PathNode *node_7 = findSubnode(node_5, "7", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, three_hash, 0);
  struct stat node_7_stats = sStat(node_7->path.str);
  node_7_stats.st_uid++;
  mustHaveRegularStats(node_7, &metadata->backup_history[1], node_7_stats, 400, three_hash, 0);

  PathNode *node_8 = findSubnode(files, "8", BH_owner_changed | BH_timestamp_changed, BPOL_track, 2, 4);
  mustHaveDirectoryStat(node_8, &metadata->current_backup);
  struct stat node_8_stats = sStat(node_8->path.str);
  node_8_stats.st_gid++;
  node_8_stats.st_mtime++;
  mustHaveDirectoryStats(node_8, &metadata->backup_history[1], node_8_stats);

  PathNode *node_9 = findSubnode(node_8, "9", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 12, (uint8_t *)"This is a file\n", 0);
  struct stat node_9_stats = sStat(node_9->path.str);
  node_9_stats.st_uid++;
  mustHaveRegularStats(node_9, &metadata->backup_history[1], node_9_stats, 15,
                       (uint8_t *)"This is a file\n", 0);

  PathNode *node_10 = findSubnode(node_8, "10", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, (uint8_t *)"GID and UID", 0);
  struct stat node_10_stats = sStat(node_10->path.str);
  node_10_stats.st_mtime++;
  mustHaveRegularStats(node_10, &metadata->backup_history[1], node_10_stats, 11,
                       (uint8_t *)"GID and UID", 0);

  PathNode *node_11 = findSubnode(node_8, "11", BH_owner_changed | BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, (uint8_t *)"", 0);
  struct stat node_11_stats = sStat(node_11->path.str);
  node_11_stats.st_uid++;
  node_11_stats.st_mode++;
  mustHaveRegularStats(node_11, &metadata->backup_history[1], node_11_stats, 0, (uint8_t *)"", 0);

  PathNode *node_12 = findSubnode(node_8, "12", BH_owner_changed | BH_permissions_changed |
                                  BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 14, some_file_hash, 0);
  struct stat node_12_stats = sStat(node_12->path.str);
  node_12_stats.st_gid++;
  node_12_stats.st_mode++;
  mustHaveRegularStats(node_12, &metadata->backup_history[1], node_12_stats, 84, some_file_hash, 0);

  PathNode *node_13 = findSubnode(files, "13", BH_owner_changed | BH_permissions_changed |
                                  BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_13, &metadata->current_backup);
  struct stat node_13_stats = sStat(node_13->path.str);
  node_13_stats.st_gid++;
  node_13_stats.st_mode++;
  node_13_stats.st_mtime++;
  mustHaveDirectoryStats(node_13, &metadata->backup_history[1], node_13_stats);

  PathNode *node_14 = findSubnode(files, "14", BH_owner_changed | BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_14, &metadata->current_backup);
  struct stat node_14_stats = sStat(node_14->path.str);
  node_14_stats.st_uid++;
  node_14_stats.st_mtime++;
  mustHaveDirectoryStats(node_14, &metadata->backup_history[1], node_14_stats);

  PathNode *node_15 = findSubnode(files, "15", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_15, &metadata->current_backup, "uid changing symlink");
  struct stat node_15_stats = sLStat(node_15->path.str);
  node_15_stats.st_uid++;
  mustHaveSymlinkStats(node_15, &metadata->backup_history[1], node_15_stats, "uid changing symlink");

  PathNode *node_16 = findSubnode(files, "16", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_16, &metadata->current_backup, "gid changing symlink");
  struct stat node_16_stats = sLStat(node_16->path.str);
  node_16_stats.st_gid++;
  mustHaveSymlinkStats(node_16, &metadata->backup_history[1], node_16_stats, "gid changing symlink");

  PathNode *node_17 = findSubnode(files, "17", BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_17, &metadata->current_backup,    "symlink-content");
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[1], "symlink content");

  PathNode *node_18 = findSubnode(files, "18", BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_18, &metadata->current_backup,    "symlink content string");
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[1], "symlink content");

  PathNode *node_19 = findSubnode(files, "19", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_19, &metadata->current_backup, "uid + content");
  struct stat node_19_stats = sLStat(node_19->path.str);
  node_19_stats.st_gid++;
  mustHaveSymlinkStats(node_19, &metadata->backup_history[1], node_19_stats, "gid + content");

  PathNode *node_20 = findSubnode(files, "20", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_20, &metadata->current_backup, "content, uid, gid ");
  struct stat node_20_stats = sLStat(node_20->path.str);
  node_20_stats.st_uid++;
  node_20_stats.st_gid++;
  mustHaveSymlinkStats(node_20, &metadata->backup_history[1], node_20_stats, "content, uid, gid");

  PathNode *node_21 = findSubnode(files, "21", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash, 0);
  struct stat node_21_stats = sStat(node_21->path.str);
  node_21_stats.st_gid++;
  mustHaveRegularStats(node_21, &metadata->backup_history[1], node_21_stats, 2100, super_hash, 0);

  PathNode *node_22 = findSubnode(files, "22", BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash, 0);
  struct stat node_22_stats = sStat(node_22->path.str);
  node_22_stats.st_mode++;
  mustHaveRegularStats(node_22, &metadata->backup_history[1], node_22_stats, 1200, data_d_hash, 0);

  PathNode *node_23 = findSubnode(files, "23", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, nested_1_hash, 0);
  struct stat node_23_stats = sStat(node_23->path.str);
  node_23_stats.st_mtime++;
  mustHaveRegularStats(node_23, &metadata->backup_history[1], node_23_stats, 144, nested_1_hash, 0);

  PathNode *node_24 = findSubnode(files, "24", BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup,    63, nested_2_hash, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[1], 56, nested_2_hash, 0);

  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);

  PathNode *node_26 = findSubnode(files, "26", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 22, nb_a_abc_1_hash, 0);
  struct stat node_26_stats = sStat(node_26->path.str);
  node_26_stats.st_gid++;
  mustHaveRegularStats(node_26, &metadata->backup_history[1], node_26_stats, 24, nb_a_abc_1_hash, 0);

  PathNode *node_27 = findSubnode(files, "27", BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, nb_manual_b_hash, 0);
  struct stat node_27_stats = sStat(node_27->path.str);
  node_27_stats.st_mode++;
  mustHaveRegularStats(node_27, &metadata->backup_history[1], node_27_stats, 21, nb_manual_b_hash, 0);

  PathNode *node_28 = findSubnode(files, "28", BH_timestamp_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2124, bin_hash, 0);
  struct stat node_28_stats = sStat(node_28->path.str);
  node_28_stats.st_mtime++;
  mustHaveRegularStats(node_28, &metadata->backup_history[1], node_28_stats, 2123, bin_hash, 0);

  PathNode *node_29 = findSubnode(files, "29", BH_owner_changed | BH_timestamp_changed |
                                  BH_content_changed | BH_fresh_hash, BPOL_track, 2, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, node_29_hash, 0);
  struct stat node_29_stats = sStat(node_29->path.str);
  node_29_stats.st_uid++;
  node_29_stats.st_mtime++;
  mustHaveRegularStats(node_29, &metadata->backup_history[1], node_29_stats, 1200, bin_c_1_hash, 0);

  PathNode *node_30 = findSubnode(files, "30", BH_owner_changed | BH_permissions_changed |
                                  BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, three_hash, 0);
  struct stat node_30_stats = sStat(node_30->path.str);
  node_30_stats.st_uid++;
  node_30_stats.st_mode++;
  node_30_stats.st_mtime++;
  mustHaveRegularStats(node_30, &metadata->backup_history[1], node_30_stats, 400, three_hash, 0);

  PathNode *node_31 = findSubnode(files, "31", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash, 0);
  struct stat node_31_stats = sStat(node_31->path.str);
  node_31_stats.st_uid++;
  node_31_stats.st_gid++;
  mustHaveRegularStats(node_31, &metadata->backup_history[1], node_31_stats, 2100, super_hash, 0);

  PathNode *node_32 = findSubnode(files, "32", BH_content_changed, BPOL_track, 2, 0);
  node_32->history->state.metadata.reg.hash[12] = '?';
  mustHaveRegularStat(node_32, &metadata->current_backup,    13, (uint8_t *)"A small file??", 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[1], 12, (uint8_t *)"A small file",   0);

  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);

  PathNode *node_34 = findSubnode(files, "34", BH_timestamp_changed | BH_content_changed |
                                  BH_fresh_hash, BPOL_track, 2, 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, (uint8_t *)"some dummy text", 0);
  struct stat node_34_stats = sStat(node_34->path.str);
  node_34_stats.st_mtime++;
  mustHaveRegularStats(node_34, &metadata->backup_history[1], node_34_stats,
                       15, (uint8_t *)"Some dummy text", 0);

  PathNode *node_35 = findSubnode(files, "35", BH_permissions_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 1, (uint8_t *)"abcdefghijkl", 0);
  struct stat node_35_stats = sStat(node_35->path.str);
  node_35_stats.st_mode++;
  mustHaveRegularStats(node_35, &metadata->backup_history[1], node_35_stats, 12, (uint8_t *)"abcdefghijkl", 0);

  PathNode *node_36 = findSubnode(files, "36", BH_owner_changed | BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, (uint8_t *)"Nano Backup", 0);
  struct stat node_36_stats = sStat(node_36->path.str);
  node_36_stats.st_gid++;
  node_36_stats.st_mode++;
  mustHaveRegularStats(node_36, &metadata->backup_history[1], node_36_stats, 11, (uint8_t *)"Nano Backup", 0);

  PathNode *node_37 = findSubnode(files, "37", BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_37, &metadata->current_backup,    0,  nested_2_hash, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[1], 56, nested_2_hash, 0);;

  PathNode *node_38 = findSubnode(files, "38", BH_content_changed, BPOL_track, 2, 0);
  node_38->history->state.metadata.reg.hash[0] = 'P';
  mustHaveRegularStat(node_38, &metadata->current_backup,    1, (uint8_t *)"PPP", 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[1], 0, (uint8_t *)"",    0);

  PathNode *node_39 = findSubnode(files, "39", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, (uint8_t *)"", 0);
  struct stat node_39_stats = sStat(node_39->path.str);
  node_39_stats.st_gid++;
  mustHaveRegularStats(node_39, &metadata->backup_history[1], node_39_stats, 0, (uint8_t *)"", 0);

  PathNode *node_40 = findSubnode(files, "40", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, (uint8_t *)"", 0);
  struct stat node_40_stats = sStat(node_40->path.str);
  node_40_stats.st_mtime++;
  mustHaveRegularStats(node_40, &metadata->backup_history[1], node_40_stats, 0, (uint8_t *)"", 0);

  PathNode *node_41 = findSubnode(files, "41", BH_permissions_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 0, (uint8_t *)"random file", 0);
  struct stat node_41_stats = sStat(node_41->path.str);
  node_41_stats.st_mode++;
  mustHaveRegularStats(node_41, &metadata->backup_history[1], node_41_stats, 11, (uint8_t *)"random file", 0);

  PathNode *node_42 = findSubnode(files, "42", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  memset(node_42->history->state.metadata.reg.hash, 'X', FILE_HASH_SIZE);
  node_42->history->state.metadata.reg.slot = 7;
  mustHaveRegularStat(node_42, &metadata->current_backup, 518, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 7);
  struct stat node_42_stats = sStat(node_42->path.str);
  node_42_stats.st_gid++;
  mustHaveRegularStats(node_42, &metadata->backup_history[1], node_42_stats, 0, (uint8_t *)"", 0);

  PathNode *node_43 = findSubnode(files, "43", BH_timestamp_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 12, data_d_hash, 0);
  struct stat node_43_stats = sStat(node_43->path.str);
  node_43_stats.st_mtime++;
  mustHaveRegularStats(node_43, &metadata->backup_history[1], node_43_stats, 1200, data_d_hash, 0);

  PathNode *node_44 = findSubnode(files, "44", BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_44, &metadata->current_backup,    20,  nested_1_hash, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[1], 144, nested_1_hash, 0);

  PathNode *node_45 = findSubnode(files, "45", BH_content_changed, BPOL_track, 2, 0);
  memset(&node_45->history->state.metadata.reg.hash[10], 'J', 10);
  node_45->history->state.metadata.reg.slot = 99;
  mustHaveRegularStat(node_45, &metadata->current_backup,    21, (uint8_t *)"Small fileJJJJJJJJJJ", 99);
  mustHaveRegularStat(node_45, &metadata->backup_history[1], 10, (uint8_t *)"Small file",           0);

  PathNode *node_46 = findSubnode(files, "46", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  memset(&node_46->history->state.metadata.reg.hash[9], '=', 11);
  node_46->history->state.metadata.reg.slot = 0;
  mustHaveRegularStat(node_46, &metadata->current_backup, 615, (uint8_t *)"Test file===========", 0);
  struct stat node_46_stats = sStat(node_46->path.str);
  node_46_stats.st_uid++;
  mustHaveRegularStats(node_46, &metadata->backup_history[1], node_46_stats, 9, (uint8_t *)"Test file", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 49);
  mustHaveRegularStat(node_7,  &metadata->current_backup,    400,  three_hash,                        0);
  mustHaveRegularStat(node_9,  &metadata->current_backup,    12,   (uint8_t *)"This is test",         0);
  mustHaveRegularStat(node_10, &metadata->current_backup,    11,   (uint8_t *)"GID and UID",          0);
  mustHaveRegularStat(node_11, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_12, &metadata->current_backup,    14,   (uint8_t *)"a short string",       0);
  mustHaveRegularStat(node_21, &metadata->current_backup,    2100, super_hash,                        0);
  mustHaveRegularStat(node_22, &metadata->current_backup,    1200, data_d_hash,                       0);
  mustHaveRegularStat(node_23, &metadata->current_backup,    144,  nested_1_hash,                     0);
  mustHaveRegularStat(node_24, &metadata->current_backup,    63,   node_24_hash,                      0);
  mustHaveRegularStat(node_26, &metadata->current_backup,    22,   node_26_hash,                      0);
  mustHaveRegularStat(node_27, &metadata->current_backup,    21,   nb_manual_b_hash,                  0);
  mustHaveRegularStat(node_28, &metadata->current_backup,    2124, node_28_hash,                      0);
  mustHaveRegularStat(node_29, &metadata->current_backup,    1200, node_29_hash,                      0);
  mustHaveRegularStat(node_30, &metadata->current_backup,    400,  three_hash,                        0);
  mustHaveRegularStat(node_31, &metadata->current_backup,    2100, super_hash,                        0);
  mustHaveRegularStat(node_32, &metadata->current_backup,    13,   (uint8_t *)"A small file.",        0);
  mustHaveRegularStat(node_34, &metadata->current_backup,    15,   (uint8_t *)"some dummy text",      0);
  mustHaveRegularStat(node_35, &metadata->current_backup,    1,    (uint8_t *)"?",                    0);
  mustHaveRegularStat(node_36, &metadata->current_backup,    11,   (uint8_t *)"Nano Backup",          0);
  mustHaveRegularStat(node_37, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_38, &metadata->current_backup,    1,    (uint8_t *)"@",                    0);
  mustHaveRegularStat(node_39, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_40, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_41, &metadata->current_backup,    0,    (uint8_t *)"",                     0);
  mustHaveRegularStat(node_42, &metadata->current_backup,    518,  node_42_hash,                      0);
  mustHaveRegularStat(node_43, &metadata->current_backup,    12,   (uint8_t *)"Large\nLarge\n",       0);
  mustHaveRegularStat(node_44, &metadata->current_backup,    20,   (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  mustHaveRegularStat(node_45, &metadata->current_backup,    21,   node_45_hash,                      0);
  mustHaveRegularStat(node_46, &metadata->current_backup,    615,  node_46_hash,                      0);

  /* Assert that the previous states got left unmodified. */
  mustHaveRegularStats(node_7, &metadata->backup_history[1], node_7_stats, 400, three_hash, 0);
  mustHaveRegularStats(node_9, &metadata->backup_history[1], node_9_stats, 15, (uint8_t *)"This is a file\n", 0);
  mustHaveRegularStats(node_10, &metadata->backup_history[1], node_10_stats, 11, (uint8_t *)"GID and UID", 0);
  mustHaveRegularStats(node_11, &metadata->backup_history[1], node_11_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_12, &metadata->backup_history[1], node_12_stats, 84, some_file_hash, 0);
  mustHaveRegularStats(node_21, &metadata->backup_history[1], node_21_stats, 2100, super_hash, 0);
  mustHaveRegularStats(node_22, &metadata->backup_history[1], node_22_stats, 1200, data_d_hash, 0);
  mustHaveRegularStats(node_23, &metadata->backup_history[1], node_23_stats, 144, nested_1_hash, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[1], 56, nested_2_hash, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);
  mustHaveRegularStats(node_26, &metadata->backup_history[1], node_26_stats, 24, nb_a_abc_1_hash, 0);
  mustHaveRegularStats(node_27, &metadata->backup_history[1], node_27_stats, 21, nb_manual_b_hash, 0);
  mustHaveRegularStats(node_28, &metadata->backup_history[1], node_28_stats, 2123, bin_hash, 0);
  mustHaveRegularStats(node_29, &metadata->backup_history[1], node_29_stats, 1200, bin_c_1_hash, 0);
  mustHaveRegularStats(node_30, &metadata->backup_history[1], node_30_stats, 400, three_hash, 0);
  mustHaveRegularStats(node_31, &metadata->backup_history[1], node_31_stats, 2100, super_hash, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);
  mustHaveRegularStats(node_34, &metadata->backup_history[1], node_34_stats, 15, (uint8_t *)"Some dummy text", 0);
  mustHaveRegularStats(node_35, &metadata->backup_history[1], node_35_stats, 12, (uint8_t *)"abcdefghijkl", 0);
  mustHaveRegularStats(node_36, &metadata->backup_history[1], node_36_stats, 11, (uint8_t *)"Nano Backup", 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[1], 56, nested_2_hash, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[1], 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_39, &metadata->backup_history[1], node_39_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_40, &metadata->backup_history[1], node_40_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_41, &metadata->backup_history[1], node_41_stats, 11, (uint8_t *)"random file", 0);
  mustHaveRegularStats(node_42, &metadata->backup_history[1], node_42_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_43, &metadata->backup_history[1], node_43_stats, 1200, data_d_hash, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[1], 144, nested_1_hash, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[1], 10, (uint8_t *)"Small file", 0);
  mustHaveRegularStats(node_46, &metadata->backup_history[1], node_46_stats, 9, (uint8_t *)"Test file", 0);
}

/** Tests the metadata written by phase 31 and cleans up. */
static void trackPostDetectionTest(String cwd_path, size_t cwd_depth,
                                   SearchNode *track_detection_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[30], cwd_depth + 47);
  checkHistPoint(metadata, 1, 1, phase_timestamps[28], 47);
  initiateBackup(metadata, track_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps[30], 45);
  checkHistPoint(metadata, 1, 1, phase_timestamps[28], 47);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_unchanged, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_0, &metadata->backup_history[0]);
  struct stat node_0_stats = sStat(node_0->path.str);
  node_0_stats.st_uid++;
  mustHaveDirectoryStats(node_0, &metadata->backup_history[1], node_0_stats);

  PathNode *node_1 = findSubnode(node_0, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_1, &metadata->backup_history[0]);
  struct stat node_1_stats = sStat(node_1->path.str);
  node_1_stats.st_gid++;
  mustHaveDirectoryStats(node_1, &metadata->backup_history[1], node_1_stats);

  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_2, &metadata->backup_history[0]);
  struct stat node_2_stats = sStat(node_2->path.str);
  node_2_stats.st_mode++;
  mustHaveDirectoryStats(node_2, &metadata->backup_history[1], node_2_stats);

  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0]);
  struct stat node_3_stats = sStat(node_3->path.str);
  node_3_stats.st_mtime++;
  mustHaveDirectoryStats(node_3, &metadata->backup_history[1], node_3_stats);

  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0]);
  struct stat node_4_stats = sStat(node_4->path.str);
  node_4_stats.st_mode++;
  node_4_stats.st_mtime++;
  mustHaveDirectoryStats(node_4, &metadata->backup_history[1], node_4_stats);

  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, BPOL_track, 2, 2);
  mustHaveDirectoryStat(node_5, &metadata->backup_history[0]);
  struct stat node_5_stats = sStat(node_5->path.str);
  node_5_stats.st_uid++;
  node_5_stats.st_mode++;
  mustHaveDirectoryStats(node_5, &metadata->backup_history[1], node_5_stats);

  PathNode *node_6 = findSubnode(node_5, "6", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0], "/dev/null");
  struct stat node_6_stats = sLStat(node_6->path.str);
  node_6_stats.st_uid++;
  mustHaveSymlinkStats(node_6, &metadata->backup_history[1], node_6_stats, "/dev/non-existing");

  PathNode *node_7 = findSubnode(node_5, "7", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_7, &metadata->backup_history[0], 400, three_hash, 0);
  struct stat node_7_stats = sStat(node_7->path.str);
  node_7_stats.st_uid++;
  mustHaveRegularStats(node_7, &metadata->backup_history[1], node_7_stats, 400, three_hash, 0);

  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, BPOL_track, 2, 4);
  mustHaveDirectoryStat(node_8, &metadata->backup_history[0]);
  struct stat node_8_stats = sStat(node_8->path.str);
  node_8_stats.st_gid++;
  node_8_stats.st_mtime++;
  mustHaveDirectoryStats(node_8, &metadata->backup_history[1], node_8_stats);

  PathNode *node_9 = findSubnode(node_8, "9", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_9, &metadata->backup_history[0], 12, (uint8_t *)"This is test", 0);
  struct stat node_9_stats = sStat(node_9->path.str);
  node_9_stats.st_uid++;
  mustHaveRegularStats(node_9, &metadata->backup_history[1], node_9_stats, 15, (uint8_t *)"This is a file\n", 0);

  PathNode *node_10 = findSubnode(node_8, "10", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_10, &metadata->backup_history[0], 11, (uint8_t *)"GID and UID", 0);
  struct stat node_10_stats = sStat(node_10->path.str);
  node_10_stats.st_mtime++;
  mustHaveRegularStats(node_10, &metadata->backup_history[1], node_10_stats, 11, (uint8_t *)"GID and UID", 0);

  PathNode *node_11 = findSubnode(node_8, "11", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_11, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_11_stats = sStat(node_11->path.str);
  node_11_stats.st_uid++;
  node_11_stats.st_mode++;
  mustHaveRegularStats(node_11, &metadata->backup_history[1], node_11_stats, 0, (uint8_t *)"", 0);

  PathNode *node_12 = findSubnode(node_8, "12", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_12, &metadata->backup_history[0], 14, (uint8_t *)"a short string", 0);
  struct stat node_12_stats = sStat(node_12->path.str);
  node_12_stats.st_gid++;
  node_12_stats.st_mode++;
  mustHaveRegularStats(node_12, &metadata->backup_history[1], node_12_stats, 84, some_file_hash, 0);

  PathNode *node_13 = findSubnode(files, "13", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_13, &metadata->backup_history[0]);
  struct stat node_13_stats = sStat(node_13->path.str);
  node_13_stats.st_gid++;
  node_13_stats.st_mode++;
  node_13_stats.st_mtime++;
  mustHaveDirectoryStats(node_13, &metadata->backup_history[1], node_13_stats);

  PathNode *node_14 = findSubnode(files, "14", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_14, &metadata->backup_history[0]);
  struct stat node_14_stats = sStat(node_14->path.str);
  node_14_stats.st_uid++;
  node_14_stats.st_mtime++;
  mustHaveDirectoryStats(node_14, &metadata->backup_history[1], node_14_stats);

  PathNode *node_15 = findSubnode(files, "15", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_15, &metadata->backup_history[0], "uid changing symlink");
  struct stat node_15_stats = sLStat(node_15->path.str);
  node_15_stats.st_uid++;
  mustHaveSymlinkStats(node_15, &metadata->backup_history[1], node_15_stats, "uid changing symlink");

  PathNode *node_16 = findSubnode(files, "16", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_16, &metadata->backup_history[0], "gid changing symlink");
  struct stat node_16_stats = sLStat(node_16->path.str);
  node_16_stats.st_gid++;
  mustHaveSymlinkStats(node_16, &metadata->backup_history[1], node_16_stats, "gid changing symlink");

  PathNode *node_17 = findSubnode(files, "17", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[0],    "symlink-content");
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[1], "symlink content");

  PathNode *node_18 = findSubnode(files, "18", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[0],    "symlink content string");
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[1], "symlink content");

  PathNode *node_19 = findSubnode(files, "19", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_19, &metadata->backup_history[0], "uid + content");
  struct stat node_19_stats = sLStat(node_19->path.str);
  node_19_stats.st_gid++;
  mustHaveSymlinkStats(node_19, &metadata->backup_history[1], node_19_stats, "gid + content");

  PathNode *node_20 = findSubnode(files, "20", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_20, &metadata->backup_history[0], "content, uid, gid ");
  struct stat node_20_stats = sLStat(node_20->path.str);
  node_20_stats.st_uid++;
  node_20_stats.st_gid++;
  mustHaveSymlinkStats(node_20, &metadata->backup_history[1], node_20_stats, "content, uid, gid");

  PathNode *node_21 = findSubnode(files, "21", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_21, &metadata->backup_history[0], 2100, super_hash, 0);
  struct stat node_21_stats = sStat(node_21->path.str);
  node_21_stats.st_gid++;
  mustHaveRegularStats(node_21, &metadata->backup_history[1], node_21_stats, 2100, super_hash, 0);

  PathNode *node_22 = findSubnode(files, "22", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_22, &metadata->backup_history[0], 1200, data_d_hash, 0);
  struct stat node_22_stats = sStat(node_22->path.str);
  node_22_stats.st_mode++;
  mustHaveRegularStats(node_22, &metadata->backup_history[1], node_22_stats, 1200, data_d_hash, 0);

  PathNode *node_23 = findSubnode(files, "23", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_23, &metadata->backup_history[0], 144, nested_1_hash, 0);
  struct stat node_23_stats = sStat(node_23->path.str);
  node_23_stats.st_mtime++;
  mustHaveRegularStats(node_23, &metadata->backup_history[1], node_23_stats, 144, nested_1_hash, 0);

  PathNode *node_24 = findSubnode(files, "24", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[0], 63, node_24_hash, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[1], 56, nested_2_hash, 0);

  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);

  PathNode *node_26 = findSubnode(files, "26", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_26, &metadata->backup_history[0], 22, node_26_hash, 0);
  struct stat node_26_stats = sStat(node_26->path.str);
  node_26_stats.st_gid++;
  mustHaveRegularStats(node_26, &metadata->backup_history[1], node_26_stats, 24, nb_a_abc_1_hash, 0);

  PathNode *node_27 = findSubnode(files, "27", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_27, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  struct stat node_27_stats = sStat(node_27->path.str);
  node_27_stats.st_mode++;
  mustHaveRegularStats(node_27, &metadata->backup_history[1], node_27_stats, 21, nb_manual_b_hash, 0);

  PathNode *node_28 = findSubnode(files, "28", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_28, &metadata->backup_history[0], 2124, node_28_hash, 0);
  struct stat node_28_stats = sStat(node_28->path.str);
  node_28_stats.st_mtime++;
  mustHaveRegularStats(node_28, &metadata->backup_history[1], node_28_stats, 2123, bin_hash, 0);

  PathNode *node_29 = findSubnode(files, "29", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_29, &metadata->backup_history[0], 1200, node_29_hash, 0);
  struct stat node_29_stats = sStat(node_29->path.str);
  node_29_stats.st_uid++;
  node_29_stats.st_mtime++;
  mustHaveRegularStats(node_29, &metadata->backup_history[1], node_29_stats, 1200, bin_c_1_hash, 0);

  PathNode *node_30 = findSubnode(files, "30", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_30, &metadata->backup_history[0], 400, three_hash, 0);
  struct stat node_30_stats = sStat(node_30->path.str);
  node_30_stats.st_uid++;
  node_30_stats.st_mode++;
  node_30_stats.st_mtime++;
  mustHaveRegularStats(node_30, &metadata->backup_history[1], node_30_stats, 400, three_hash, 0);

  PathNode *node_31 = findSubnode(files, "31", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_31, &metadata->backup_history[0], 2100, super_hash, 0);
  struct stat node_31_stats = sStat(node_31->path.str);
  node_31_stats.st_uid++;
  node_31_stats.st_gid++;
  mustHaveRegularStats(node_31, &metadata->backup_history[1], node_31_stats, 2100, super_hash, 0);

  PathNode *node_32 = findSubnode(files, "32", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[0], 13, (uint8_t *)"A small file.", 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);

  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);

  PathNode *node_34 = findSubnode(files, "34", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_34, &metadata->backup_history[0], 15, (uint8_t *)"some dummy text", 0);
  struct stat node_34_stats = sStat(node_34->path.str);
  node_34_stats.st_mtime++;
  mustHaveRegularStats(node_34, &metadata->backup_history[1], node_34_stats, 15, (uint8_t *)"Some dummy text", 0);

  PathNode *node_35 = findSubnode(files, "35", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_35, &metadata->backup_history[0], 1, (uint8_t *)"?", 0);
  struct stat node_35_stats = sStat(node_35->path.str);
  node_35_stats.st_mode++;
  mustHaveRegularStats(node_35, &metadata->backup_history[1], node_35_stats, 12, (uint8_t *)"abcdefghijkl", 0);

  PathNode *node_36 = findSubnode(files, "36", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_36, &metadata->backup_history[0], 11, (uint8_t *)"Nano Backup", 0);
  struct stat node_36_stats = sStat(node_36->path.str);
  node_36_stats.st_gid++;
  node_36_stats.st_mode++;
  mustHaveRegularStats(node_36, &metadata->backup_history[1], node_36_stats, 11, (uint8_t *)"Nano Backup", 0);

  PathNode *node_37 = findSubnode(files, "37", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[1], 56, nested_2_hash, 0);

  PathNode *node_38 = findSubnode(files, "38", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[0], 1, (uint8_t *)"@", 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *node_39 = findSubnode(files, "39", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_39, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_39_stats = sStat(node_39->path.str);
  node_39_stats.st_gid++;
  mustHaveRegularStats(node_39, &metadata->backup_history[1], node_39_stats, 0, (uint8_t *)"", 0);

  PathNode *node_40 = findSubnode(files, "40", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_40, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_40_stats = sStat(node_40->path.str);
  node_40_stats.st_mtime++;
  mustHaveRegularStats(node_40, &metadata->backup_history[1], node_40_stats, 0, (uint8_t *)"", 0);

  PathNode *node_41 = findSubnode(files, "41", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_41, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_41_stats = sStat(node_41->path.str);
  node_41_stats.st_mode++;
  mustHaveRegularStats(node_41, &metadata->backup_history[1], node_41_stats, 11, (uint8_t *)"random file", 0);

  PathNode *node_42 = findSubnode(files, "42", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_42, &metadata->backup_history[0], 518, node_42_hash, 0);
  struct stat node_42_stats = sStat(node_42->path.str);
  node_42_stats.st_gid++;
  mustHaveRegularStats(node_42, &metadata->backup_history[1], node_42_stats, 0, (uint8_t *)"", 0);

  PathNode *node_43 = findSubnode(files, "43", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_43, &metadata->backup_history[0], 12, (uint8_t *)"Large\nLarge\n", 0);
  struct stat node_43_stats = sStat(node_43->path.str);
  node_43_stats.st_mtime++;
  mustHaveRegularStats(node_43, &metadata->backup_history[1], node_43_stats, 1200, data_d_hash, 0);

  PathNode *node_44 = findSubnode(files, "44", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[0], 20, (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[1], 144, nested_1_hash, 0);

  PathNode *node_45 = findSubnode(files, "45", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[0], 21, node_45_hash, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[1], 10, (uint8_t *)"Small file", 0);

  PathNode *node_46 = findSubnode(files, "46", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_46, &metadata->backup_history[0], 615, node_46_hash, 0);
  struct stat node_46_stats = sStat(node_46->path.str);
  node_46_stats.st_uid++;
  mustHaveRegularStats(node_46, &metadata->backup_history[1], node_46_stats, 9, (uint8_t *)"Test file", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 49);
}

/** Prepares replacing a directory with a file/symlink. */
static void initNoneFiletypeChange(String cwd_path, size_t cwd_depth,
                                   SearchNode *none_filetype_node)
{
  /* Generate various dummy files. */
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/a/b");
  makeDir("tmp/files/a/b/2");
  makeDir("tmp/files/a/d");
  makeDir("tmp/files/e");
  makeDir("tmp/files/e/f");
  makeDir("tmp/files/e/f/g");
  generateFile("tmp/files/a/b/1",   "foo bar", 1);
  generateFile("tmp/files/a/b/2/1", "Foo",     6);
  generateFile("tmp/files/a/c",     "nested ", 8);
  generateFile("tmp/files/a/d/1",   "BAR",     4);
  generateFile("tmp/files/e/f/h",   "Large\n", 200);
  makeSymlink("non-existing.txt", "tmp/files/e/f/i");

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, none_filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 15);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 15);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 2);

  PathNode *a = findSubnode(files, "a", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryCached(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(b, &metadata->current_backup);
  PathNode *b_1 = findSubnode(b, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(b_1, &metadata->current_backup, 7, NULL, 0);
  PathNode *b_2 = findSubnode(b, "2", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b_2, &metadata->current_backup);
  PathNode *b_2_1 = findSubnode(b_2, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2_1, &metadata->current_backup, 18, NULL, 0);
  PathNode *c = findSubnode(a, "c", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(c, &metadata->current_backup, 56, NULL, 0);
  PathNode *d = findSubnode(a, "d", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(d, &metadata->current_backup);
  PathNode *d_1 = findSubnode(d, "1", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(d_1, &metadata->current_backup, 12, NULL, 0);

  PathNode *e = findSubnode(files, "e", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryCached(e, &metadata->current_backup);
  PathNode *f = findSubnode(e, "f", BH_added, BPOL_none, 1, 3);
  mustHaveDirectoryCached(f, &metadata->current_backup);
  PathNode *g = findSubnode(f, "g", BH_added, BPOL_track, 1, 0);
  mustHaveDirectoryCached(g, &metadata->current_backup);
  PathNode *h = findSubnode(f, "h", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(h, &metadata->current_backup, 1200, NULL, 0);
  PathNode *i = findSubnode(f, "i", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(i, &metadata->current_backup, "non-existing.txt");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 7);
  mustHaveRegularCached(b_1,   &metadata->current_backup, 7,    (uint8_t *)"foo bar",            0);
  mustHaveRegularCached(b_2_1, &metadata->current_backup, 18,   (uint8_t *)"FooFooFooFooFooFoo", 0);
  mustHaveRegularCached(c,     &metadata->current_backup, 56,   nested_2_hash,                   0);
  mustHaveRegularCached(d_1,   &metadata->current_backup, 12,   (uint8_t *)"BARBARBARBAR",       0);
  mustHaveRegularCached(h,     &metadata->current_backup, 1200, data_d_hash,                     0);
}

/** Removes "tmp/files/a" generated by initNoneFiletypeChange(). */
static void removeNoneFiletypeA(void)
{
  removePath("tmp/files/a/d/1");
  removePath("tmp/files/a/d");
  removePath("tmp/files/a/c");
  removePath("tmp/files/a/b/2/1");
  removePath("tmp/files/a/b/2");
  removePath("tmp/files/a/b/1");
  removePath("tmp/files/a/b");
  removePath("tmp/files/a");
}

/** Replaces a directory with a regular file and modifies the current
  metadata. */
static void change1NoneFiletypeChange(String cwd_path, size_t cwd_depth,
                                      SearchNode *none_filetype_node)
{
  /* Replace directory with regular file. */
  removeNoneFiletypeA();
  generateFile("tmp/files/a", "a/b/c/", 7);
  removePath("tmp/files/e/f/g");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 15);
  initiateBackup(metadata, none_filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 6);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 10);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 2);

  PathNode *a = findSubnode(files, "a", BH_directory_to_regular, BPOL_none, 1, 3);
  mustHaveDirectoryCached(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(b, &metadata->backup_history[0]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[0], 7, (uint8_t *)"foo bar", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b_2, &metadata->backup_history[0]);
  PathNode *b_2_1 = findSubnode(b_2, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2_1, &metadata->backup_history[0], 18, (uint8_t *)"FooFooFooFooFooFoo", 0);
  PathNode *c = findSubnode(a, "c", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(c, &metadata->backup_history[0], 56, nested_2_hash, 0);
  PathNode *d = findSubnode(a, "d", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(d, &metadata->backup_history[0]);
  PathNode *d_1 = findSubnode(d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(d_1, &metadata->backup_history[0], 12, (uint8_t *)"BARBARBARBAR", 0);

  PathNode *e = findSubnode(files, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryCached(e, &metadata->current_backup);
  PathNode *f = findSubnode(e, "f", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryCached(f, &metadata->current_backup);
  PathNode *g = findSubnode(f, "g", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(g, &metadata->current_backup);
  mustHaveDirectoryCached(g, &metadata->backup_history[0]);
  PathNode *h = findSubnode(f, "h", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(h, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *i = findSubnode(f, "i", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(i, &metadata->backup_history[0], "non-existing.txt");

  /* Modify various path nodes. */
  e->history->state.uid++;
  e->history->state.metadata.dir.timestamp++;

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 7);
}

/** Like change1NoneFiletypeChange(), but replaces a directory with a
  symlink to a regular file. */
static void change2NoneFiletypeChange(String cwd_path, size_t cwd_depth,
                                      SearchNode *none_filetype_node)
{
  /* Replace directory with symlink to regular file. */
  removePath("tmp/files/e/f/h");
  removePath("tmp/files/e/f/i");
  removePath("tmp/files/e/f");
  removePath("tmp/files/e");
  makeSymlink("a", "tmp/files/e");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 6);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 10);
  initiateBackup(metadata, none_filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 4);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 10);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 2);

  PathNode *a = findSubnode(files, "a", BH_directory_to_regular, BPOL_none, 1, 3);
  mustHaveDirectoryCached(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(b, &metadata->backup_history[1]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[1], 7, (uint8_t *)"foo bar", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b_2, &metadata->backup_history[1]);
  PathNode *b_2_1 = findSubnode(b_2, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2_1, &metadata->backup_history[1], 18, (uint8_t *)"FooFooFooFooFooFoo", 0);
  PathNode *c = findSubnode(a, "c", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(c, &metadata->backup_history[1], 56, nested_2_hash, 0);
  PathNode *d = findSubnode(a, "d", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);
  PathNode *d_1 = findSubnode(d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(d_1, &metadata->backup_history[1], 12, (uint8_t *)"BARBARBARBAR", 0);

  PathNode *e = findSubnode(files, "e", BH_directory_to_regular, BPOL_none, 1, 1);
  struct stat e_stats = cachedStat(e->path, sStat);
  e_stats.st_uid++;
  e_stats.st_mtime++;
  mustHaveDirectoryStats(e, &metadata->current_backup, e_stats);
  PathNode *f = findSubnode(e, "f", BH_removed, BPOL_none, 1, 3);
  mustHaveDirectoryCached(f, &metadata->backup_history[0]);
  PathNode *g = findSubnode(f, "g", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(g, &metadata->backup_history[0]);
  mustHaveDirectoryCached(g, &metadata->backup_history[1]);
  PathNode *h = findSubnode(f, "h", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(h, &metadata->backup_history[1], 1200, data_d_hash, 0);
  PathNode *i = findSubnode(f, "i", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(i, &metadata->backup_history[1], "non-existing.txt");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 7);
}

/** Tests the metadata written by change2NoneFiletypeChange(). */
static void postNoneFiletypeChange(String cwd_path, size_t cwd_depth,
                                   SearchNode *none_filetype_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[backup_counter - 3], 10);
  initiateBackup(metadata, none_filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 4);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[backup_counter - 3], 10);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 2);

  PathNode *a = findSubnode(files, "a", BH_directory_to_regular, BPOL_none, 1, 3);
  mustHaveDirectoryCached(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(b, &metadata->backup_history[2]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[2], 7, (uint8_t *)"foo bar", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b_2, &metadata->backup_history[2]);
  PathNode *b_2_1 = findSubnode(b_2, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2_1, &metadata->backup_history[2], 18, (uint8_t *)"FooFooFooFooFooFoo", 0);
  PathNode *c = findSubnode(a, "c", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(c, &metadata->backup_history[2], 56, nested_2_hash, 0);
  PathNode *d = findSubnode(a, "d", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(d, &metadata->backup_history[2]);
  PathNode *d_1 = findSubnode(d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(d_1, &metadata->backup_history[2], 12, (uint8_t *)"BARBARBARBAR", 0);

  PathNode *e = findSubnode(files, "e", BH_directory_to_regular, BPOL_none, 1, 1);
  struct stat e_stats = cachedStat(e->path, sStat);
  e_stats.st_uid++;
  e_stats.st_mtime++;
  mustHaveDirectoryStats(e, &metadata->current_backup, e_stats);
  PathNode *f = findSubnode(e, "f", BH_removed, BPOL_none, 1, 3);
  mustHaveDirectoryCached(f, &metadata->backup_history[1]);
  PathNode *g = findSubnode(f, "g", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(g, &metadata->backup_history[1]);
  mustHaveDirectoryCached(g, &metadata->backup_history[2]);
  PathNode *h = findSubnode(f, "h", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(h, &metadata->backup_history[2], 1200, data_d_hash, 0);
  PathNode *i = findSubnode(f, "i", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(i, &metadata->backup_history[2], "non-existing.txt");

  /* Modify various path nodes. */
  e->history->state.uid--;
  e->history->state.metadata.dir.timestamp--;

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 7);
}

/** Restores test files to their initial state and cleans up. */
static void restoreNoneFiletypeChange(String cwd_path, size_t cwd_depth,
                                      SearchNode *none_filetype_node)
{
  /* Load the metadata. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 3], 2);
  checkHistPoint(metadata, 2, 2, phase_timestamps[backup_counter - 4], 10);

  /* Restore all files and initiate the backup. */
  removePath("tmp/files/a");
  removePath("tmp/files/e");
  restoreWithTimeRecursively(metadata->paths);
  initiateBackup(metadata, none_filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 6);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 15);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 3], 1);
  checkHistPoint(metadata, 2, 2, phase_timestamps[backup_counter - 4], 10);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 2);

  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(b, &metadata->backup_history[2]);
  PathNode *b_1 = findSubnode(b, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(b_1, &metadata->backup_history[2], 7, (uint8_t *)"foo bar", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(b_2, &metadata->backup_history[2]);
  PathNode *b_2_1 = findSubnode(b_2, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(b_2_1, &metadata->backup_history[2], 18, (uint8_t *)"FooFooFooFooFooFoo", 0);
  PathNode *c = findSubnode(a, "c", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(c, &metadata->backup_history[2], 56, nested_2_hash, 0);
  PathNode *d = findSubnode(a, "d", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(d, &metadata->backup_history[2]);
  PathNode *d_1 = findSubnode(d, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(d_1, &metadata->backup_history[2], 12, (uint8_t *)"BARBARBARBAR", 0);

  PathNode *e = findSubnode(files, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  PathNode *f = findSubnode(e, "f", BH_unchanged, BPOL_none, 1, 3);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  PathNode *g = findSubnode(f, "g", BH_added, BPOL_track, 3, 0);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveNonExisting(g, &metadata->backup_history[1]);
  mustHaveDirectoryStat(g, &metadata->backup_history[2]);
  PathNode *h = findSubnode(f, "h", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(h, &metadata->backup_history[2], 1200, data_d_hash, 0);
  PathNode *i = findSubnode(f, "i", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLStat(i, &metadata->backup_history[2], "non-existing.txt");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 7);
}

/** Prepares the testing of filetype changes. */
static void initFiletypeChange(String cwd_path, size_t cwd_depth,
                               SearchNode *filetype_node,
                               BackupPolicy policy)
{
  /* Prepare the test files. */
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/5");
  makeDir("tmp/files/6");
  makeDir("tmp/files/6/a");
  makeDir("tmp/files/7");
  makeDir("tmp/files/7/a");
  makeDir("tmp/files/7/b");
  makeDir("tmp/files/7/c");
  makeDir("tmp/files/7/d");
  makeDir("tmp/files/8");
  makeDir("tmp/files/8/a");
  makeDir("tmp/files/8/a/b");
  makeDir("tmp/files/8/c");
  makeDir("tmp/files/8/c/d");
  makeDir("tmp/files/8/e");
  makeDir("tmp/files/8/e/f");
  makeDir("tmp/files/8/e/f/1");
  makeDir("tmp/files/9");
  generateFile("tmp/files/1",         "DummyFile",   1);
  generateFile("tmp/files/3",         "a/b/c/",      7);
  generateFile("tmp/files/6/a/1",     "X",           20);
  generateFile("tmp/files/6/2",       "FOO",         2);
  generateFile("tmp/files/6/3",       "0",           2123);
  generateFile("tmp/files/7/a/1",     "nested ",     9);
  generateFile("tmp/files/7/b/1",     "nested ",     2);
  generateFile("tmp/files/7/b/2",     "empty\n",     200);
  generateFile("tmp/files/7/c/2",     "dummy",       1);
  generateFile("tmp/files/7/d/1",     "DUMMY-",      3);
  generateFile("tmp/files/8/a/b/1",   "_FILE_",      2);
  generateFile("tmp/files/8/c/d/1",   "empty\n",     200);
  generateFile("tmp/files/8/e/f/1/1", "nano backup", 1);
  generateFile("tmp/files/8/e/f/1/2", "NanoBackup",  1);
  makeSymlink("target",           "tmp/files/2");
  makeSymlink("/dev/nano-backup", "tmp/files/4");
  makeSymlink("/home",            "tmp/files/7/c/1");
  makeSymlink("1",                "tmp/files/7/d/2");

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 37);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 37);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 9);

  PathNode *node_1 = findSubnode(files, "1", BH_added, policy, 1, 0);
  mustHaveRegularCached(node_1, &metadata->current_backup, 9, NULL, 0);
  PathNode *node_2 = findSubnode(files, "2", BH_added, policy, 1, 0);
  mustHaveSymlinkLCached(node_2, &metadata->current_backup, "target");
  PathNode *node_3 = findSubnode(files, "3", BH_added, policy, 1, 0);
  mustHaveRegularCached(node_3, &metadata->current_backup, 42, NULL, 0);
  PathNode *node_4 = findSubnode(files, "4", BH_added, policy, 1, 0);
  mustHaveSymlinkLCached(node_4, &metadata->current_backup, "/dev/nano-backup");
  PathNode *node_5 = findSubnode(files, "5", BH_added, policy, 1, 0);
  mustHaveDirectoryCached(node_5, &metadata->current_backup);

  PathNode *node_6 = findSubnode(files, "6", BH_added, policy, 1, 3);
  mustHaveDirectoryCached(node_6, &metadata->current_backup);
  PathNode *node_6_a = findSubnode(node_6, "a", BH_added, policy, 1, 1);
  mustHaveDirectoryCached(node_6_a, &metadata->current_backup);
  PathNode *node_6_a_1 = findSubnode(node_6_a, "1", BH_added, policy, 1, 0);
  mustHaveRegularCached(node_6_a_1, &metadata->current_backup, 20, NULL, 0);
  PathNode *node_6_2 = findSubnode(node_6, "2", BH_added, policy, 1, 0);
  mustHaveRegularCached(node_6_2, &metadata->current_backup, 6, NULL, 0);
  PathNode *node_6_3 = findSubnode(node_6, "3", BH_added, policy, 1, 0);
  mustHaveRegularCached(node_6_3, &metadata->current_backup, 2123, NULL, 0);

  PathNode *node_7 = findSubnode(files, "7", BH_added, policy, 1, 4);
  mustHaveDirectoryCached(node_7, &metadata->current_backup);
  PathNode *node_7_a = findSubnode(node_7, "a", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(node_7_a, &metadata->current_backup);
  PathNode *node_7_a_1 = findSubnode(node_7_a, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(node_7_a_1, &metadata->current_backup, 63, NULL, 0);
  PathNode *node_7_b = findSubnode(node_7, "b", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_7_b, &metadata->current_backup);
  PathNode *node_7_b_1 = findSubnode(node_7_b, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(node_7_b_1, &metadata->current_backup, 14, NULL, 0);
  PathNode *node_7_b_2 = findSubnode(node_7_b, "2", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(node_7_b_2, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_7_c = findSubnode(node_7, "c", BH_added, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(node_7_c, &metadata->current_backup);
  PathNode *node_7_c_1 = findSubnode(node_7_c, "1", BH_added, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(node_7_c_1, &metadata->current_backup, "/home");
  PathNode *node_7_c_2 = findSubnode(node_7_c, "2", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_7_c_2, &metadata->current_backup, 5, NULL, 0);
  PathNode *node_7_d = findSubnode(node_7, "d", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(node_7_d, &metadata->current_backup);
  PathNode *node_7_d_1 = findSubnode(node_7_d, "1", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_7_d_1, &metadata->current_backup, 18, NULL, 0);
  PathNode *node_7_d_2 = findSubnode(node_7_d, "2", BH_added, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(node_7_d_2, &metadata->current_backup, "1");

  PathNode *node_8 = findSubnode(files, "8", BH_added, policy, 1, 3);
  mustHaveDirectoryCached(node_8, &metadata->current_backup);
  PathNode *node_8_a = findSubnode(node_8, "a", BH_added, policy, 1, 1);
  mustHaveDirectoryCached(node_8_a, &metadata->current_backup);
  PathNode *node_8_a_b = findSubnode(node_8_a, "b", BH_added, BPOL_track, 1, 1);
  mustHaveDirectoryCached(node_8_a_b, &metadata->current_backup);
  PathNode *node_8_a_b_1 = findSubnode(node_8_a_b, "1", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_8_a_b_1, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_8_c = findSubnode(node_8, "c", BH_added, policy, 1, 1);
  mustHaveDirectoryCached(node_8_c, &metadata->current_backup);
  PathNode *node_8_c_d = findSubnode(node_8_c, "d", BH_added, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(node_8_c_d, &metadata->current_backup);
  PathNode *node_8_c_d_1 = findSubnode(node_8_c_d, "1", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_8_c_d_1, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_8_e = findSubnode(node_8, "e", BH_added, policy, 1, 1);
  mustHaveDirectoryCached(node_8_e, &metadata->current_backup);
  PathNode *node_8_e_f = findSubnode(node_8_e, "f", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(node_8_e_f, &metadata->current_backup);
  PathNode *node_8_e_f_1 = findSubnode(node_8_e_f, "1", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_8_e_f_1, &metadata->current_backup);
  PathNode *node_8_e_f_1_1 = findSubnode(node_8_e_f_1, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_1, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_8_e_f_1_2 = findSubnode(node_8_e_f_1, "2", BH_added, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_2, &metadata->current_backup, 10, NULL, 0);

  PathNode *node_9 = findSubnode(files, "9", BH_added, policy, 1, 0);
  mustHaveDirectoryCached(node_9, &metadata->current_backup);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 13);
  mustHaveRegularCached(node_1,         &metadata->current_backup, 9,    (uint8_t *)"DummyFile",            0);
  mustHaveRegularCached(node_3,         &metadata->current_backup, 42,   test_c_hash,                       0);
  mustHaveRegularCached(node_6_a_1,     &metadata->current_backup, 20,   (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 0);
  mustHaveRegularCached(node_6_2,       &metadata->current_backup, 6,    (uint8_t *)"FOOFOO",               0);
  mustHaveRegularCached(node_6_3,       &metadata->current_backup, 2123, bin_hash,                          0);
  mustHaveRegularCached(node_7_a_1,     &metadata->current_backup, 63,   node_24_hash,                      0);
  mustHaveRegularCached(node_7_b_1,     &metadata->current_backup, 14,   (uint8_t *)"nested nested ",       0);
  mustHaveRegularCached(node_7_b_2,     &metadata->current_backup, 1200, bin_c_1_hash,                      0);
  mustHaveRegularCached(node_7_c_2,     &metadata->current_backup, 5,    (uint8_t *)"dummy",                0);
  mustHaveRegularCached(node_7_d_1,     &metadata->current_backup, 18,   (uint8_t *)"DUMMY-DUMMY-DUMMY-",   0);
  mustHaveRegularCached(node_8_a_b_1,   &metadata->current_backup, 12,   (uint8_t *)"_FILE__FILE_",         0);
  mustHaveRegularCached(node_8_c_d_1,   &metadata->current_backup, 1200, bin_c_1_hash,                      0);
  mustHaveRegularCached(node_8_e_f_1_1, &metadata->current_backup, 11,   (uint8_t *)"nano backup",          0);
  mustHaveRegularCached(node_8_e_f_1_2, &metadata->current_backup, 10,   (uint8_t *)"NanoBackup",           0);
}

/** Modifies the test files and metadata in such a way that subsequent
  backups will detect filetype changes. */
static void modifyFiletypeChange(String cwd_path, size_t cwd_depth,
                                 SearchNode *filetype_node,
                                 BackupPolicy policy)
{
  /* Remove some files. */
  removePath("tmp/files/7/a/1");
  removePath("tmp/files/7/a");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 37);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 37);
  initiateBackup(metadata, filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 4);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 37);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 35);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 9);

  PathNode *node_1 = findSubnode(files, "1", BH_unchanged, policy, 1, 0);
  mustHaveRegularCached(node_1, &metadata->backup_history[0], 9, (uint8_t *)"DummyFile", 0);
  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLCached(node_2, &metadata->backup_history[0], "target");
  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, policy, 1, 0);
  mustHaveRegularCached(node_3, &metadata->backup_history[0], 42, test_c_hash, 0);
  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLCached(node_4, &metadata->backup_history[0], "/dev/nano-backup");
  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryCached(node_5, &metadata->backup_history[0]);

  PathNode *node_6 = findSubnode(files, "6", BH_unchanged, policy, 1, 3);
  mustHaveDirectoryCached(node_6, &metadata->backup_history[0]);
  PathNode *node_6_a = findSubnode(node_6, "a", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryCached(node_6_a, &metadata->backup_history[0]);
  PathNode *node_6_a_1 = findSubnode(node_6_a, "1", BH_unchanged, policy, 1, 0);
  mustHaveRegularCached(node_6_a_1, &metadata->backup_history[0], 20, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 0);
  PathNode *node_6_2 = findSubnode(node_6, "2", BH_unchanged, policy, 1, 0);
  mustHaveRegularCached(node_6_2, &metadata->backup_history[0], 6, (uint8_t *)"FOOFOO", 0);
  PathNode *node_6_3 = findSubnode(node_6, "3", BH_unchanged, policy, 1, 0);
  mustHaveRegularCached(node_6_3, &metadata->backup_history[0], 2123, bin_hash, 0);

  PathNode *node_7 = findSubnode(files, "7", BH_unchanged, policy, 1, 4);
  mustHaveDirectoryCached(node_7, &metadata->backup_history[0]);
  PathNode *node_7_a = findSubnode(node_7, "a", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(node_7_a, &metadata->current_backup);
  mustHaveDirectoryCached(node_7_a, &metadata->backup_history[0]);
  PathNode *node_7_a_1 = findSubnode(node_7_a, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_a_1, &metadata->current_backup);
  mustHaveRegularCached(node_7_a_1, &metadata->backup_history[0], 63, node_24_hash, 0);
  PathNode *node_7_b = findSubnode(node_7, "b", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_7_b, &metadata->backup_history[0]);
  PathNode *node_7_b_1 = findSubnode(node_7_b, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(node_7_b_1, &metadata->backup_history[0], 14, (uint8_t *)"nested nested ", 0);
  PathNode *node_7_b_2 = findSubnode(node_7_b, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(node_7_b_2, &metadata->backup_history[0], 1200, bin_c_1_hash, 0);
  PathNode *node_7_c = findSubnode(node_7, "c", BH_unchanged, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(node_7_c, &metadata->backup_history[0]);
  PathNode *node_7_c_1 = findSubnode(node_7_c, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(node_7_c_1, &metadata->backup_history[0], "/home");
  PathNode *node_7_c_2 = findSubnode(node_7_c, "2", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_7_c_2, &metadata->backup_history[0], 5, (uint8_t *)"dummy", 0);
  PathNode *node_7_d = findSubnode(node_7, "d", BH_unchanged, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(node_7_d, &metadata->backup_history[0]);
  PathNode *node_7_d_1 = findSubnode(node_7_d, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_7_d_1, &metadata->backup_history[0], 18, (uint8_t *)"DUMMY-DUMMY-DUMMY-", 0);
  PathNode *node_7_d_2 = findSubnode(node_7_d, "2", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(node_7_d_2, &metadata->backup_history[0], "1");

  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, policy, 1, 3);
  mustHaveDirectoryCached(node_8, &metadata->backup_history[0]);
  PathNode *node_8_a = findSubnode(node_8, "a", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryCached(node_8_a, &metadata->backup_history[0]);
  PathNode *node_8_a_b = findSubnode(node_8_a, "b", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryCached(node_8_a_b, &metadata->backup_history[0]);
  PathNode *node_8_a_b_1 = findSubnode(node_8_a_b, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_8_a_b_1, &metadata->backup_history[0], 12, (uint8_t *)"_FILE__FILE_", 0);
  PathNode *node_8_c = findSubnode(node_8, "c", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryCached(node_8_c, &metadata->backup_history[0]);
  PathNode *node_8_c_d = findSubnode(node_8_c, "d", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(node_8_c_d, &metadata->backup_history[0]);
  PathNode *node_8_c_d_1 = findSubnode(node_8_c_d, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_8_c_d_1, &metadata->backup_history[0], 1200, bin_c_1_hash, 0);
  PathNode *node_8_e = findSubnode(node_8, "e", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryCached(node_8_e, &metadata->backup_history[0]);
  PathNode *node_8_e_f = findSubnode(node_8_e, "f", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(node_8_e_f, &metadata->backup_history[0]);
  PathNode *node_8_e_f_1 = findSubnode(node_8_e_f, "1", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_8_e_f_1, &metadata->backup_history[0]);
  PathNode *node_8_e_f_1_1 = findSubnode(node_8_e_f_1, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_1, &metadata->backup_history[0], 11, (uint8_t *)"nano backup", 0);
  PathNode *node_8_e_f_1_2 = findSubnode(node_8_e_f_1, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_2, &metadata->backup_history[0], 10, (uint8_t *)"NanoBackup", 0);

  PathNode *node_9 = findSubnode(files, "9", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryCached(node_9, &metadata->backup_history[0]);

  /* Modify various path nodes. */
  removePath("tmp/files/1");
  makeSymlink("NewSymlink", "tmp/files/1");

  removePath("tmp/files/2");
  generateFile("tmp/files/2", "Backup\n", 74);

  node_3->history->state.gid++;
  removePath("tmp/files/3");
  makeDir("tmp/files/3");
  makeDir("tmp/files/3/a");
  makeDir("tmp/files/3/a/c");
  generateFile("tmp/files/3/a/b",   "nano-backup", 1);
  generateFile("tmp/files/3/a/c/1", "test 123",    1);
  generateFile("tmp/files/3/a/c/2", "TEST_TEST",   1);

  removePath("tmp/files/4");
  makeDir("tmp/files/4");
  makeDir("tmp/files/4/a");
  makeDir("tmp/files/4/a/c");
  generateFile("tmp/files/4/a/b",   "backup", 2);
  generateFile("tmp/files/4/a/c/1", "q",      21);
  generateFile("tmp/files/4/a/c/2", "=",      20);

  node_5->history->state.metadata.dir.mode++;
  removePath("tmp/files/5");
  generateFile("tmp/files/5", "?", 13);

  removePath("tmp/files/6/3");
  removePath("tmp/files/6/2");
  removePath("tmp/files/6/a/1");
  removePath("tmp/files/6/a");
  removePath("tmp/files/6");
  makeSymlink("3", "tmp/files/6");

  removePath("tmp/files/7/b/2");
  removePath("tmp/files/7/b/1");
  removePath("tmp/files/7/b");
  removePath("tmp/files/7/c/2");
  removePath("tmp/files/7/c/1");
  removePath("tmp/files/7/c");
  removePath("tmp/files/7/d/2");
  removePath("tmp/files/7/d/1");
  removePath("tmp/files/7/d");
  removePath("tmp/files/7");
  generateFile("tmp/files/7", "", 0);

  removePath("tmp/files/8/a/b/1");
  removePath("tmp/files/8/a/b");
  removePath("tmp/files/8/a");
  removePath("tmp/files/8/c/d/1");
  removePath("tmp/files/8/c/d");
  removePath("tmp/files/8/c");
  removePath("tmp/files/8/e/f/1/2");
  removePath("tmp/files/8/e/f/1/1");
  removePath("tmp/files/8/e/f/1");
  removePath("tmp/files/8/e/f");
  removePath("tmp/files/8/e");
  removePath("tmp/files/8");
  node_8->history->state.metadata.dir.mode++;
  makeSymlink("2", "tmp/files/8");

  removePath("tmp/files/9");
  makeSymlink("/dev/null", "tmp/files/9");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 13);
}

/** Checks the changes injected by modifyFiletypeChange(). */
static void changeFiletypeChange(String cwd_path, size_t cwd_depth,
                                 SearchNode *filetype_node,
                                 BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 37);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 35);
  initiateBackup(metadata, filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 21);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 0);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 9);

  PathNode *node_1 = findSubnode(files, "1", BH_regular_to_symlink, policy, 1, 0);
  mustHaveSymlinkLStat(node_1, &metadata->current_backup, "NewSymlink");
  PathNode *node_2 = findSubnode(files, "2", BH_symlink_to_regular, policy, 1, 0);
  mustHaveRegularStat(node_2, &metadata->current_backup, 518, NULL, 0);
  PathNode *node_3 = findSubnode(files, "3", BH_regular_to_directory, policy, 1, 1);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  PathNode *node_3_a = findSubnode(node_3, "a", BH_added, policy, 1, 2);
  mustHaveDirectoryStat(node_3_a, &metadata->current_backup);
  PathNode *node_3_b = findSubnode(node_3_a, "b", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(node_3_b, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_3_c = findSubnode(node_3_a, "c", BH_added, policy, 1, 2);
  mustHaveDirectoryStat(node_3_c, &metadata->current_backup);
  PathNode *node_3_1 = findSubnode(node_3_c, "1", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(node_3_1, &metadata->current_backup, 8, NULL, 0);
  PathNode *node_3_2 = findSubnode(node_3_c, "2", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(node_3_2, &metadata->current_backup, 9, NULL, 0);
  PathNode *node_4 = findSubnode(files, "4", BH_symlink_to_directory, policy, 1, 1);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  PathNode *node_4_a = findSubnode(node_4, "a", BH_added, policy, 1, 2);
  mustHaveDirectoryStat(node_4_a, &metadata->current_backup);
  PathNode *node_4_b = findSubnode(node_4_a, "b", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_4_b, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_4_c = findSubnode(node_4_a, "c", BH_added, policy, 1, 2);
  mustHaveDirectoryStat(node_4_c, &metadata->current_backup);
  PathNode *node_4_1 = findSubnode(node_4_c, "1", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_4_1, &metadata->current_backup, 21, NULL, 0);
  PathNode *node_4_2 = findSubnode(node_4_c, "2", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_4_2, &metadata->current_backup, 20, NULL, 0);
  PathNode *node_5 = findSubnode(files, "5", BH_directory_to_regular, policy, 1, 0);
  mustHaveRegularStat(node_5, &metadata->current_backup, 13, NULL, 0);

  PathNode *node_6 = findSubnode(files, "6", BH_directory_to_symlink, policy, 1, 3);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "3");
  PathNode *node_6_a = findSubnode(node_6, "a", BH_not_part_of_repository, policy, 1, 1);
  mustHaveDirectoryCached(node_6_a, &metadata->backup_history[1]);
  PathNode *node_6_a_1 = findSubnode(node_6_a, "1", BH_not_part_of_repository, policy, 1, 0);
  mustHaveRegularCached(node_6_a_1, &metadata->backup_history[1], 20, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 0);
  PathNode *node_6_2 = findSubnode(node_6, "2", BH_not_part_of_repository, policy, 1, 0);
  mustHaveRegularCached(node_6_2, &metadata->backup_history[1], 6, (uint8_t *)"FOOFOO", 0);
  PathNode *node_6_3 = findSubnode(node_6, "3", BH_not_part_of_repository, policy, 1, 0);
  mustHaveRegularCached(node_6_3, &metadata->backup_history[1], 2123, bin_hash, 0);

  PathNode *node_7 = findSubnode(files, "7", BH_directory_to_regular, policy, 1, 4);
  mustHaveRegularStat(node_7, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_7_a = findSubnode(node_7, "a", BH_not_part_of_repository, BPOL_track, 2, 1);
  mustHaveNonExisting(node_7_a, &metadata->backup_history[0]);
  mustHaveDirectoryCached(node_7_a, &metadata->backup_history[1]);
  PathNode *node_7_a_1 = findSubnode(node_7_a, "1", BH_not_part_of_repository, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_a_1, &metadata->backup_history[0]);
  mustHaveRegularCached(node_7_a_1, &metadata->backup_history[1], 63, node_24_hash, 0);
  PathNode *node_7_b = findSubnode(node_7, "b", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_7_b, &metadata->backup_history[1]);
  PathNode *node_7_b_1 = findSubnode(node_7_b, "1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(node_7_b_1, &metadata->backup_history[1], 14, (uint8_t *)"nested nested ", 0);
  PathNode *node_7_b_2 = findSubnode(node_7_b, "2", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(node_7_b_2, &metadata->backup_history[1], 1200, bin_c_1_hash, 0);
  PathNode *node_7_c = findSubnode(node_7, "c", BH_not_part_of_repository, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(node_7_c, &metadata->backup_history[1]);
  PathNode *node_7_c_1 = findSubnode(node_7_c, "1", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(node_7_c_1, &metadata->backup_history[1], "/home");
  PathNode *node_7_c_2 = findSubnode(node_7_c, "2", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_7_c_2, &metadata->backup_history[1], 5, (uint8_t *)"dummy", 0);
  PathNode *node_7_d = findSubnode(node_7, "d", BH_not_part_of_repository, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(node_7_d, &metadata->backup_history[1]);
  PathNode *node_7_d_1 = findSubnode(node_7_d, "1", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_7_d_1, &metadata->backup_history[1], 18, (uint8_t *)"DUMMY-DUMMY-DUMMY-", 0);
  PathNode *node_7_d_2 = findSubnode(node_7_d, "2", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(node_7_d_2, &metadata->backup_history[1], "1");

  PathNode *node_8 = findSubnode(files, "8", BH_directory_to_regular, policy, 1, 3);
  mustHaveRegularStat(node_8, &metadata->current_backup, 518, NULL, 0);
  PathNode *node_8_a = findSubnode(node_8, "a", BH_not_part_of_repository, policy, 1, 1);
  mustHaveDirectoryCached(node_8_a, &metadata->backup_history[1]);
  PathNode *node_8_a_b = findSubnode(node_8_a, "b", BH_not_part_of_repository, BPOL_track, 1, 1);
  mustHaveDirectoryCached(node_8_a_b, &metadata->backup_history[1]);
  PathNode *node_8_a_b_1 = findSubnode(node_8_a_b, "1", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_8_a_b_1, &metadata->backup_history[1], 12, (uint8_t *)"_FILE__FILE_", 0);
  PathNode *node_8_c = findSubnode(node_8, "c", BH_not_part_of_repository, policy, 1, 1);
  mustHaveDirectoryCached(node_8_c, &metadata->backup_history[1]);
  PathNode *node_8_c_d = findSubnode(node_8_c, "d", BH_not_part_of_repository, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(node_8_c_d, &metadata->backup_history[1]);
  PathNode *node_8_c_d_1 = findSubnode(node_8_c_d, "1", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_8_c_d_1, &metadata->backup_history[1], 1200, bin_c_1_hash, 0);
  PathNode *node_8_e = findSubnode(node_8, "e", BH_not_part_of_repository, policy, 1, 1);
  mustHaveDirectoryCached(node_8_e, &metadata->backup_history[1]);
  PathNode *node_8_e_f = findSubnode(node_8_e, "f", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(node_8_e_f, &metadata->backup_history[1]);
  PathNode *node_8_e_f_1 = findSubnode(node_8_e_f, "1", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_8_e_f_1, &metadata->backup_history[1]);
  PathNode *node_8_e_f_1_1 = findSubnode(node_8_e_f_1, "1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_1, &metadata->backup_history[1], 11, (uint8_t *)"nano backup", 0);
  PathNode *node_8_e_f_1_2 = findSubnode(node_8_e_f_1, "2", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_2, &metadata->backup_history[1], 10, (uint8_t *)"NanoBackup", 0);

  PathNode *node_9 = findSubnode(files, "9", BH_directory_to_symlink, policy, 1, 0);
  mustHaveSymlinkLStat(node_9, &metadata->current_backup, "/dev/null");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 18);
  mustHaveRegularStat(node_2,   &metadata->current_backup, 518, node_42_hash,                      0);
  mustHaveRegularStat(node_3_b, &metadata->current_backup, 11,  (uint8_t *)"nano-backup",          0);
  mustHaveRegularStat(node_3_1, &metadata->current_backup, 8,   (uint8_t *)"test 123",             0);
  mustHaveRegularStat(node_3_2, &metadata->current_backup, 9,   (uint8_t *)"TEST_TEST",            0);
  mustHaveRegularStat(node_4_b, &metadata->current_backup, 12,  (uint8_t *)"backupbackup",         0);
  mustHaveRegularStat(node_4_1, &metadata->current_backup, 21,  node_45_hash,                      0);
  mustHaveRegularStat(node_4_2, &metadata->current_backup, 20,  (uint8_t *)"====================", 0);
  mustHaveRegularStat(node_5,   &metadata->current_backup, 13,  (uint8_t *)"?????????????",        0);
  mustHaveRegularStat(node_7,   &metadata->current_backup, 0,   (uint8_t *)"K",                    0);
  mustHaveRegularStat(node_8,   &metadata->current_backup, 518, node_42_hash,                      0);
}

/** Tests the metadata written by changeFiletypeChange() and cleans up. */
static void postFiletypeChange(String cwd_path, size_t cwd_depth,
                               SearchNode *filetype_node,
                               BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 21);
  initiateBackup(metadata, filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 19);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 9);

  PathNode *node_1 = findSubnode(files, "1", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_1, &metadata->backup_history[0], "NewSymlink");
  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_2, &metadata->backup_history[0], 518, node_42_hash, 0);

  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0]);
  PathNode *node_3_a = findSubnode(node_3, "a", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_3_a, &metadata->backup_history[0]);
  PathNode *node_3_b = findSubnode(node_3_a, "b", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_3_b, &metadata->backup_history[0], 11, (uint8_t *)"nano-backup", 0);
  PathNode *node_3_c = findSubnode(node_3_a, "c", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_3_c, &metadata->backup_history[0]);
  PathNode *node_3_1 = findSubnode(node_3_c, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(node_3_1, &metadata->backup_history[0], 8, (uint8_t *)"test 123", 0);
  PathNode *node_3_2 = findSubnode(node_3_c, "2", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(node_3_2, &metadata->backup_history[0], 9, (uint8_t *)"TEST_TEST", 0);

  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0]);
  PathNode *node_4_a = findSubnode(node_4, "a", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_4_a, &metadata->backup_history[0]);
  PathNode *node_4_b = findSubnode(node_4_a, "b", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_4_b, &metadata->backup_history[0], 12, (uint8_t *)"backupbackup", 0);
  PathNode *node_4_c = findSubnode(node_4_a, "c", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_4_c, &metadata->backup_history[0]);
  PathNode *node_4_1 = findSubnode(node_4_c, "1", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_4_1, &metadata->backup_history[0], 21, node_45_hash, 0);
  PathNode *node_4_2 = findSubnode(node_4_c, "2", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_4_2, &metadata->backup_history[0], 20, (uint8_t *)"====================", 0);

  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_5, &metadata->backup_history[0], 13, (uint8_t *)"?????????????", 0);
  PathNode *node_6 = findSubnode(files, "6", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0], "3");
  PathNode *node_7 = findSubnode(files, "7", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->backup_history[0], 0, (uint8_t *)"K", 0);
  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_8, &metadata->backup_history[0], 518, node_42_hash, 0);
  PathNode *node_9 = findSubnode(files, "9", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_9, &metadata->backup_history[0], "/dev/null");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 18);
}

/** Checks the changes injected by modifyFiletypeChange() for the track
  policy. */
static void trackFiletypeChange(String cwd_path, size_t cwd_depth,
                                SearchNode *filetype_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 37);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 35);
  initiateBackup(metadata, filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 32);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 47);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 35);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 9);

  PathNode *node_1 = findSubnode(files, "1", BH_regular_to_symlink, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_1, &metadata->current_backup, "NewSymlink");
  mustHaveRegularCached(node_1, &metadata->backup_history[1], 9, (uint8_t *)"DummyFile", 0);
  PathNode *node_2 = findSubnode(files, "2", BH_symlink_to_regular, BPOL_track, 2, 0);
  mustHaveRegularStat(node_2, &metadata->current_backup, 518, NULL, 0);
  mustHaveSymlinkLCached(node_2, &metadata->backup_history[1], "target");

  PathNode *node_3 = findSubnode(files, "3", BH_regular_to_directory, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  struct stat node_3_stats = cachedStat(node_3->path, sStat);
  node_3_stats.st_gid++;
  mustHaveRegularStats(node_3, &metadata->backup_history[1], node_3_stats, 42, test_c_hash, 0);
  PathNode *node_3_a = findSubnode(node_3, "a", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_3_a, &metadata->current_backup);
  PathNode *node_3_b = findSubnode(node_3_a, "b", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(node_3_b, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_3_c = findSubnode(node_3_a, "c", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_3_c, &metadata->current_backup);
  PathNode *node_3_1 = findSubnode(node_3_c, "1", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(node_3_1, &metadata->current_backup, 8, NULL, 0);
  PathNode *node_3_2 = findSubnode(node_3_c, "2", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(node_3_2, &metadata->current_backup, 9, NULL, 0);

  PathNode *node_4 = findSubnode(files, "4", BH_symlink_to_directory, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  mustHaveSymlinkLCached(node_4, &metadata->backup_history[1], "/dev/nano-backup");
  PathNode *node_4_a = findSubnode(node_4, "a", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_4_a, &metadata->current_backup);
  PathNode *node_4_b = findSubnode(node_4_a, "b", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(node_4_b, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_4_c = findSubnode(node_4_a, "c", BH_added, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_4_c, &metadata->current_backup);
  PathNode *node_4_1 = findSubnode(node_4_c, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(node_4_1, &metadata->current_backup, 21, NULL, 0);
  PathNode *node_4_2 = findSubnode(node_4_c, "2", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(node_4_2, &metadata->current_backup, 20, NULL, 0);

  PathNode *node_5 = findSubnode(files, "5", BH_directory_to_regular, BPOL_track, 2, 0);
  mustHaveRegularStat(node_5, &metadata->current_backup, 13, NULL, 0);
  struct stat node_5_stats = cachedStat(node_5->path, sStat);
  node_5_stats.st_mode++;
  mustHaveDirectoryStats(node_5, &metadata->backup_history[1], node_5_stats);

  PathNode *node_6 = findSubnode(files, "6", BH_directory_to_symlink, BPOL_track, 2, 3);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "3");
  mustHaveDirectoryCached(node_6, &metadata->backup_history[1]);
  PathNode *node_6_a = findSubnode(node_6, "a", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(node_6_a, &metadata->current_backup);
  mustHaveDirectoryCached(node_6_a, &metadata->backup_history[1]);
  PathNode *node_6_a_1 = findSubnode(node_6_a, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(node_6_a_1, &metadata->current_backup);
  mustHaveRegularCached(node_6_a_1, &metadata->backup_history[1], 20, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 0);
  PathNode *node_6_2 = findSubnode(node_6, "2", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(node_6_2, &metadata->current_backup);
  mustHaveRegularCached(node_6_2, &metadata->backup_history[1], 6, (uint8_t *)"FOOFOO", 0);
  PathNode *node_6_3 = findSubnode(node_6, "3", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(node_6_3, &metadata->current_backup);
  mustHaveRegularCached(node_6_3, &metadata->backup_history[1], 2123, bin_hash, 0);

  PathNode *node_7 = findSubnode(files, "7", BH_directory_to_regular, BPOL_track, 2, 4);
  mustHaveRegularStat(node_7, &metadata->current_backup, 0, NULL, 0);
  mustHaveDirectoryCached(node_7, &metadata->backup_history[1]);
  PathNode *node_7_a = findSubnode(node_7, "a", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(node_7_a, &metadata->backup_history[0]);
  mustHaveDirectoryCached(node_7_a, &metadata->backup_history[1]);
  PathNode *node_7_a_1 = findSubnode(node_7_a, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_a_1, &metadata->backup_history[0]);
  mustHaveRegularCached(node_7_a_1, &metadata->backup_history[1], 63, node_24_hash, 0);
  PathNode *node_7_b = findSubnode(node_7, "b", BH_removed, BPOL_track, 2, 2);
  mustHaveNonExisting(node_7_b, &metadata->current_backup);
  mustHaveDirectoryCached(node_7_b, &metadata->backup_history[1]);
  PathNode *node_7_b_1 = findSubnode(node_7_b, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_b_1, &metadata->current_backup);
  mustHaveRegularCached(node_7_b_1, &metadata->backup_history[1], 14, (uint8_t *)"nested nested ", 0);
  PathNode *node_7_b_2 = findSubnode(node_7_b, "2", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_b_2, &metadata->current_backup);
  mustHaveRegularCached(node_7_b_2, &metadata->backup_history[1], 1200, bin_c_1_hash, 0);
  PathNode *node_7_c = findSubnode(node_7, "c", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(node_7_c, &metadata->backup_history[1]);
  PathNode *node_7_c_1 = findSubnode(node_7_c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(node_7_c_1, &metadata->backup_history[1], "/home");
  PathNode *node_7_c_2 = findSubnode(node_7_c, "2", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_7_c_2, &metadata->backup_history[1], 5, (uint8_t *)"dummy", 0);
  PathNode *node_7_d = findSubnode(node_7, "d", BH_removed, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(node_7_d, &metadata->backup_history[1]);
  PathNode *node_7_d_1 = findSubnode(node_7_d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_7_d_1, &metadata->backup_history[1], 18, (uint8_t *)"DUMMY-DUMMY-DUMMY-", 0);
  PathNode *node_7_d_2 = findSubnode(node_7_d, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(node_7_d_2, &metadata->backup_history[1], "1");

  PathNode *node_8 = findSubnode(files, "8", BH_directory_to_regular, BPOL_track, 2, 3);
  mustHaveRegularStat(node_8, &metadata->current_backup, 518, NULL, 0);
  struct stat node_8_stats = cachedStat(node_8->path, sStat);
  node_8_stats.st_mode++;
  mustHaveDirectoryStats(node_8, &metadata->backup_history[1], node_8_stats);
  PathNode *node_8_a = findSubnode(node_8, "a", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_a, &metadata->current_backup);
  mustHaveDirectoryCached(node_8_a, &metadata->backup_history[1]);
  PathNode *node_8_a_b = findSubnode(node_8_a, "b", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_a_b, &metadata->current_backup);
  mustHaveDirectoryCached(node_8_a_b, &metadata->backup_history[1]);
  PathNode *node_8_a_b_1 = findSubnode(node_8_a_b, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_8_a_b_1, &metadata->backup_history[1], 12, (uint8_t *)"_FILE__FILE_", 0);
  PathNode *node_8_c = findSubnode(node_8, "c", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_c, &metadata->current_backup);
  mustHaveDirectoryCached(node_8_c, &metadata->backup_history[1]);
  PathNode *node_8_c_d = findSubnode(node_8_c, "d", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(node_8_c_d, &metadata->backup_history[1]);
  PathNode *node_8_c_d_1 = findSubnode(node_8_c_d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_8_c_d_1, &metadata->backup_history[1], 1200, bin_c_1_hash, 0);
  PathNode *node_8_e = findSubnode(node_8, "e", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_e, &metadata->current_backup);
  mustHaveDirectoryCached(node_8_e, &metadata->backup_history[1]);
  PathNode *node_8_e_f = findSubnode(node_8_e, "f", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(node_8_e_f, &metadata->backup_history[1]);
  PathNode *node_8_e_f_1 = findSubnode(node_8_e_f, "1", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_8_e_f_1, &metadata->backup_history[1]);
  PathNode *node_8_e_f_1_1 = findSubnode(node_8_e_f_1, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_1, &metadata->backup_history[1], 11, (uint8_t *)"nano backup", 0);
  PathNode *node_8_e_f_1_2 = findSubnode(node_8_e_f_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_2, &metadata->backup_history[1], 10, (uint8_t *)"NanoBackup", 0);

  PathNode *node_9 = findSubnode(files, "9", BH_directory_to_symlink, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_9, &metadata->current_backup, "/dev/null");
  mustHaveDirectoryCached(node_9, &metadata->backup_history[1]);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 18);
  mustHaveRegularStat(node_2,   &metadata->current_backup, 518, node_42_hash,                      0);
  mustHaveRegularStat(node_3_b, &metadata->current_backup, 11,  (uint8_t *)"nano-backup",          0);
  mustHaveRegularStat(node_3_1, &metadata->current_backup, 8,   (uint8_t *)"test 123",             0);
  mustHaveRegularStat(node_3_2, &metadata->current_backup, 9,   (uint8_t *)"TEST_TEST",            0);
  mustHaveRegularStat(node_4_b, &metadata->current_backup, 12,  (uint8_t *)"backupbackup",         0);
  mustHaveRegularStat(node_4_1, &metadata->current_backup, 21,  node_45_hash,                      0);
  mustHaveRegularStat(node_4_2, &metadata->current_backup, 20,  (uint8_t *)"====================", 0);
  mustHaveRegularStat(node_5,   &metadata->current_backup, 13,  (uint8_t *)"?????????????",        0);
  mustHaveRegularStat(node_7,   &metadata->current_backup, 0,   (uint8_t *)"K",                    0);
  mustHaveRegularStat(node_8,   &metadata->current_backup, 518, node_42_hash,                      0);
}

/** Tests the metadata written by changeFiletypeChange(). It takes the
  following additional argument:

  @param completed_runs The count of subsequent runs this function has
  completed.
*/
static void trackFiletypeChangePost(String cwd_path, size_t cwd_depth,
                                    SearchNode *filetype_node,
                                    size_t completed_runs)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 47);
  size_t off = completed_runs > 0;

  if(completed_runs > 0)
  {
    checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 2);
  }
  else
  {
    checkHistPoint(metadata, 0, 0 , phase_timestamps[backup_counter - 1], cwd_depth + 32);
  }

  checkHistPoint(metadata, 1 + off, 1 + off, phase_timestamps[backup_counter - 2 - completed_runs], 2);
  checkHistPoint(metadata, 2 + off, 2 + off, phase_timestamps[backup_counter - 3 - completed_runs], 35);
  initiateBackup(metadata, filetype_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 3 + off);
  assert_true(metadata->total_path_count == cwd_depth + 47);

  if(completed_runs > 0)
  {
    checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 0);
  }

  checkHistPoint(metadata, 0 + off, 0 + off, phase_timestamps[backup_counter - 1 - completed_runs], 30);
  checkHistPoint(metadata, 1 + off, 1 + off, phase_timestamps[backup_counter - 2 - completed_runs], 2);
  checkHistPoint(metadata, 2 + off, 2 + off, phase_timestamps[backup_counter - 3 - completed_runs], 35);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 9);

  PathNode *node_1 = findSubnode(files, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_1, &metadata->backup_history[0 + off], "NewSymlink");
  mustHaveRegularCached(node_1, &metadata->backup_history[2 + off], 9, (uint8_t *)"DummyFile", 0);
  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_2, &metadata->backup_history[0 + off], 518, node_42_hash, 0);
  mustHaveSymlinkLCached(node_2, &metadata->backup_history[2 + off], "target");

  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0 + off]);
  struct stat node_3_stats = cachedStat(node_3->path, sStat);
  node_3_stats.st_gid++;
  mustHaveRegularStats(node_3, &metadata->backup_history[2 + off], node_3_stats, 42, test_c_hash, 0);
  PathNode *node_3_a = findSubnode(node_3, "a", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_3_a, &metadata->backup_history[0 + off]);
  PathNode *node_3_b = findSubnode(node_3_a, "b", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_3_b, &metadata->backup_history[0 + off], 11, (uint8_t *)"nano-backup", 0);
  PathNode *node_3_c = findSubnode(node_3_a, "c", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_3_c, &metadata->backup_history[0 + off]);
  PathNode *node_3_1 = findSubnode(node_3_c, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(node_3_1, &metadata->backup_history[0 + off], 8, (uint8_t *)"test 123", 0);
  PathNode *node_3_2 = findSubnode(node_3_c, "2", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(node_3_2, &metadata->backup_history[0 + off], 9, (uint8_t *)"TEST_TEST", 0);

  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0 + off]);
  mustHaveSymlinkLCached(node_4, &metadata->backup_history[2 + off], "/dev/nano-backup");
  PathNode *node_4_a = findSubnode(node_4, "a", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_4_a, &metadata->backup_history[0 + off]);
  PathNode *node_4_b = findSubnode(node_4_a, "b", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_4_b, &metadata->backup_history[0 + off], 12, (uint8_t *)"backupbackup", 0);
  PathNode *node_4_c = findSubnode(node_4_a, "c", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(node_4_c, &metadata->backup_history[0 + off]);
  PathNode *node_4_1 = findSubnode(node_4_c, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_4_1, &metadata->backup_history[0 + off], 21, node_45_hash, 0);
  PathNode *node_4_2 = findSubnode(node_4_c, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_4_2, &metadata->backup_history[0 + off], 20, (uint8_t *)"====================", 0);

  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_5, &metadata->backup_history[0 + off], 13, (uint8_t *)"?????????????", 0);
  struct stat node_5_stats = cachedStat(node_5->path, sStat);
  node_5_stats.st_mode++;
  mustHaveDirectoryStats(node_5, &metadata->backup_history[2 + off], node_5_stats);

  PathNode *node_6 = findSubnode(files, "6", BH_unchanged, BPOL_track, 2, 3);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0 + off], "3");
  mustHaveDirectoryCached(node_6, &metadata->backup_history[2 + off]);
  PathNode *node_6_a = findSubnode(node_6, "a", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(node_6_a, &metadata->backup_history[0 + off]);
  mustHaveDirectoryCached(node_6_a, &metadata->backup_history[2 + off]);
  PathNode *node_6_a_1 = findSubnode(node_6_a, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(node_6_a_1, &metadata->backup_history[0 + off]);
  mustHaveRegularCached(node_6_a_1, &metadata->backup_history[2 + off], 20, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 0);
  PathNode *node_6_2 = findSubnode(node_6, "2", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(node_6_2, &metadata->backup_history[0 + off]);
  mustHaveRegularCached(node_6_2, &metadata->backup_history[2 + off], 6, (uint8_t *)"FOOFOO", 0);
  PathNode *node_6_3 = findSubnode(node_6, "3", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(node_6_3, &metadata->backup_history[0 + off]);
  mustHaveRegularCached(node_6_3, &metadata->backup_history[2 + off], 2123, bin_hash, 0);

  PathNode *node_7 = findSubnode(files, "7", BH_unchanged, BPOL_track, 2, 4);
  mustHaveRegularStat(node_7, &metadata->backup_history[0 + off], 0, (uint8_t *)"K", 0);
  mustHaveDirectoryCached(node_7, &metadata->backup_history[2 + off]);
  PathNode *node_7_a = findSubnode(node_7, "a", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(node_7_a, &metadata->backup_history[1 + off]);
  mustHaveDirectoryCached(node_7_a, &metadata->backup_history[2 + off]);
  PathNode *node_7_a_1 = findSubnode(node_7_a, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_a_1, &metadata->backup_history[1 + off]);
  mustHaveRegularCached(node_7_a_1, &metadata->backup_history[2 + off], 63, node_24_hash, 0);
  PathNode *node_7_b = findSubnode(node_7, "b", BH_unchanged, BPOL_track, 2, 2);
  mustHaveNonExisting(node_7_b, &metadata->backup_history[0 + off]);
  mustHaveDirectoryCached(node_7_b, &metadata->backup_history[2 + off]);
  PathNode *node_7_b_1 = findSubnode(node_7_b, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_b_1, &metadata->backup_history[0 + off]);
  mustHaveRegularCached(node_7_b_1, &metadata->backup_history[2 + off], 14, (uint8_t *)"nested nested ", 0);
  PathNode *node_7_b_2 = findSubnode(node_7_b, "2", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(node_7_b_2, &metadata->backup_history[0 + off]);
  mustHaveRegularCached(node_7_b_2, &metadata->backup_history[2 + off], 1200, bin_c_1_hash, 0);
  PathNode *node_7_c = findSubnode(node_7, "c", BH_removed, BPOL_copy, 1, 2);
  mustHaveDirectoryCached(node_7_c, &metadata->backup_history[2 + off]);
  PathNode *node_7_c_1 = findSubnode(node_7_c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveSymlinkLCached(node_7_c_1, &metadata->backup_history[2 + off], "/home");
  PathNode *node_7_c_2 = findSubnode(node_7_c, "2", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_7_c_2, &metadata->backup_history[2 + off], 5, (uint8_t *)"dummy", 0);
  PathNode *node_7_d = findSubnode(node_7, "d", BH_removed, BPOL_mirror, 1, 2);
  mustHaveDirectoryCached(node_7_d, &metadata->backup_history[2 + off]);
  PathNode *node_7_d_1 = findSubnode(node_7_d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_7_d_1, &metadata->backup_history[2 + off], 18, (uint8_t *)"DUMMY-DUMMY-DUMMY-", 0);
  PathNode *node_7_d_2 = findSubnode(node_7_d, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveSymlinkLCached(node_7_d_2, &metadata->backup_history[2 + off], "1");

  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, BPOL_track, 2, 3);
  mustHaveRegularStat(node_8, &metadata->backup_history[0 + off], 518, node_42_hash, 0);
  struct stat node_8_stats = cachedStat(node_8->path, sStat);
  node_8_stats.st_mode++;
  mustHaveDirectoryStats(node_8, &metadata->backup_history[2 + off], node_8_stats);
  PathNode *node_8_a = findSubnode(node_8, "a", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_a, &metadata->backup_history[0 + off]);
  mustHaveDirectoryCached(node_8_a, &metadata->backup_history[2 + off]);
  PathNode *node_8_a_b = findSubnode(node_8_a, "b", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_a_b, &metadata->backup_history[0 + off]);
  mustHaveDirectoryCached(node_8_a_b, &metadata->backup_history[2 + off]);
  PathNode *node_8_a_b_1 = findSubnode(node_8_a_b, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(node_8_a_b_1, &metadata->backup_history[2 + off], 12, (uint8_t *)"_FILE__FILE_", 0);
  PathNode *node_8_c = findSubnode(node_8, "c", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_c, &metadata->backup_history[0 + off]);
  mustHaveDirectoryCached(node_8_c, &metadata->backup_history[2 + off]);
  PathNode *node_8_c_d = findSubnode(node_8_c, "d", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(node_8_c_d, &metadata->backup_history[2 + off]);
  PathNode *node_8_c_d_1 = findSubnode(node_8_c_d, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(node_8_c_d_1, &metadata->backup_history[2 + off], 1200, bin_c_1_hash, 0);
  PathNode *node_8_e = findSubnode(node_8, "e", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(node_8_e, &metadata->backup_history[0 + off]);
  mustHaveDirectoryCached(node_8_e, &metadata->backup_history[2 + off]);
  PathNode *node_8_e_f = findSubnode(node_8_e, "f", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(node_8_e_f, &metadata->backup_history[2 + off]);
  PathNode *node_8_e_f_1 = findSubnode(node_8_e_f, "1", BH_removed, BPOL_track, 1, 2);
  mustHaveDirectoryCached(node_8_e_f_1, &metadata->backup_history[2 + off]);
  PathNode *node_8_e_f_1_1 = findSubnode(node_8_e_f_1, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_1, &metadata->backup_history[2 + off], 11, (uint8_t *)"nano backup", 0);
  PathNode *node_8_e_f_1_2 = findSubnode(node_8_e_f_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(node_8_e_f_1_2, &metadata->backup_history[2 + off], 10, (uint8_t *)"NanoBackup", 0);

  PathNode *node_9 = findSubnode(files, "9", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_9, &metadata->backup_history[0 + off], "/dev/null");
  mustHaveDirectoryCached(node_9, &metadata->backup_history[2 + off]);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 18);
}

/** Prepares policy change test from BPOL_none. */
static void policyChangeFromNoneInit(String cwd_path, size_t cwd_depth,
                                     SearchNode *change_from_none_init)
{
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/b");
  makeDir("tmp/files/c");
  makeDir("tmp/files/d");
  makeDir("tmp/files/e");
  makeDir("tmp/files/f");
  makeDir("tmp/files/g");
  makeDir("tmp/files/h");
  makeDir("tmp/files/h/1");
  makeDir("tmp/files/h/3");
  generateFile("tmp/files/a/1",   "test file", 1);
  generateFile("tmp/files/b/1",   "_123_",     1);
  generateFile("tmp/files/c/1",   "abcdef",    1);
  generateFile("tmp/files/d/1",   "foo-bar",   1);
  generateFile("tmp/files/e/1",   "SomeFile",  1);
  generateFile("tmp/files/f/1",   "somefile",  1);
  generateFile("tmp/files/g/1",   "1 + 1 = 2", 1);
  generateFile("tmp/files/h/1/2", ".",         5);
  generateFile("tmp/files/h/3/4", "%",         11);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_from_none_init);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 21);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 21);

  /* Populate stat cache. */
  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 8);

  PathNode *b = findSubnode(files, "b", BH_added, BPOL_none, 1, 1);
  cachedStat(b->path, sStat);
  cachedStat(findSubnode(b, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *d = findSubnode(files, "d", BH_added, BPOL_none, 1, 1);
  cachedStat(d->path, sStat);
  cachedStat(findSubnode(d, "1", BH_added, BPOL_track, 1, 0)->path, sStat);

  PathNode *f = findSubnode(files, "f", BH_added, BPOL_none, 1, 1);
  cachedStat(f->path, sStat);
  cachedStat(findSubnode(f, "1", BH_added, BPOL_track, 1, 0)->path, sStat);

  PathNode *g = findSubnode(files, "g", BH_added, BPOL_none, 1, 1);
  cachedStat(g->path, sStat);
  cachedStat(findSubnode(g, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *h = findSubnode(files, "h", BH_added, BPOL_none, 1, 2);
  cachedStat(h->path, sStat);
  PathNode *h_1 = findSubnode(h, "1", BH_added, BPOL_copy, 1, 1);
  cachedStat(h_1->path, sStat);
  cachedStat(findSubnode(h_1, "2", BH_added, BPOL_track, 1, 0)->path, sStat);
  PathNode *h_3 = findSubnode(h, "3", BH_added, BPOL_mirror, 1, 1);
  cachedStat(h_3->path, sStat);
  cachedStat(findSubnode(h_3, "4", BH_added, BPOL_track, 1, 0)->path, sStat);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove some files. */
  removePath("tmp/files/b/1");
  removePath("tmp/files/b");
  removePath("tmp/files/d/1");
  removePath("tmp/files/d");
  removePath("tmp/files/f/1");
  removePath("tmp/files/f");
  removePath("tmp/files/g/1");
  removePath("tmp/files/g");
  removePath("tmp/files/h/1/2");
  removePath("tmp/files/h/1");
  removePath("tmp/files/h/3/4");
  removePath("tmp/files/h/3");
  removePath("tmp/files/h");

  /* Initiate another backup. */
  metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 21);
  initiateBackup(metadata, change_from_none_init);

  /* Check the other backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 5);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 16);

  /* Finish the other backup. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Finishes policy change test from BPOL_none. */
static void policyChangeFromNoneChange(String cwd_path, size_t cwd_depth,
                                       SearchNode *change_from_none_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 5);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 16);
  initiateBackup(metadata, change_from_none_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 6);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 19);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 3);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 14);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 8);

  PathNode *a = findSubnode(files, "a", BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(a, &metadata->backup_history[0]);
  PathNode *a_1 = findSubnode(a, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(a_1, &metadata->backup_history[1], 9, (uint8_t *)"test file", 0);

  PathNode *b = findSubnode(files, "b", BH_removed | BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b, &metadata->backup_history[1]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[1], 5, (uint8_t *)"_123_", 0);

  PathNode *c = findSubnode(files, "c", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[0]);
  PathNode *c_1 = findSubnode(c, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(c_1, &metadata->backup_history[1], 6, (uint8_t *)"abcdef", 0);

  PathNode *d = findSubnode(files, "d", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);
  PathNode *d_1 = findSubnode(d, "1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(d_1, &metadata->backup_history[1], 7, (uint8_t *)"foo-bar", 0);

  PathNode *e = findSubnode(files, "e", BH_policy_changed, BPOL_track, 1, 1);
  mustHaveDirectoryStat(e, &metadata->backup_history[0]);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[1], 8, (uint8_t *)"SomeFile", 0);

  PathNode *f = findSubnode(files, "f", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(f, &metadata->current_backup);
  mustHaveDirectoryCached(f, &metadata->backup_history[1]);
  PathNode *f_1 = findSubnode(f, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(f_1, &metadata->current_backup);
  mustHaveRegularCached(f_1, &metadata->backup_history[1], 8, (uint8_t *)"somefile", 0);

  PathNode *g = findSubnode(files, "g", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(g, &metadata->current_backup);
  mustHaveDirectoryCached(g, &metadata->backup_history[1]);
  PathNode *g_1 = findSubnode(g, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(g_1, &metadata->backup_history[1], 9, (uint8_t *)"1 + 1 = 2", 0);

  PathNode *h = findSubnode(files, "h", BH_removed | BH_policy_changed, BPOL_track, 2, 2);
  mustHaveNonExisting(h, &metadata->current_backup);
  mustHaveDirectoryCached(h, &metadata->backup_history[1]);
  PathNode *h_1 = findSubnode(h, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(h_1, &metadata->backup_history[1]);
  PathNode *h_2 = findSubnode(h_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_2, &metadata->backup_history[1], 5, (uint8_t *)".....", 0);
  PathNode *h_3 = findSubnode(h, "3", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(h_3, &metadata->backup_history[1]);
  PathNode *h_4 = findSubnode(h_3, "4", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_4, &metadata->backup_history[1], 11, (uint8_t *)"%%%%%%%%%%%", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Checks the metadata written by the previous test and cleans up. */
static void policyChangeFromNonePost(String cwd_path, size_t cwd_depth,
                                     SearchNode *change_from_none_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 19);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 6);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 3);
  checkHistPoint(metadata, 2, 2, phase_timestamps[backup_counter - 3], 14);
  initiateBackup(metadata, change_from_none_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth + 19);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 3);
  checkHistPoint(metadata, 2, 2, phase_timestamps[backup_counter - 3], 14);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 7);

  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(a, &metadata->backup_history[1]);
  PathNode *a_1 = findSubnode(a, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(a_1, &metadata->backup_history[2], 9, (uint8_t *)"test file", 0);

  PathNode *b = findSubnode(files, "b", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b, &metadata->backup_history[2]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[2], 5, (uint8_t *)"_123_", 0);

  PathNode *c = findSubnode(files, "c", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(c_1, &metadata->backup_history[2], 6, (uint8_t *)"abcdef", 0);

  PathNode *e = findSubnode(files, "e", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryStat(e, &metadata->backup_history[1]);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[2], 8, (uint8_t *)"SomeFile", 0);

  PathNode *f = findSubnode(files, "f", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(f, &metadata->backup_history[0]);
  mustHaveDirectoryCached(f, &metadata->backup_history[2]);
  PathNode *f_1 = findSubnode(f, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(f_1, &metadata->backup_history[0]);
  mustHaveRegularCached(f_1, &metadata->backup_history[2], 8, (uint8_t *)"somefile", 0);

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(g, &metadata->backup_history[0]);
  mustHaveDirectoryCached(g, &metadata->backup_history[2]);
  PathNode *g_1 = findSubnode(g, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(g_1, &metadata->backup_history[2], 9, (uint8_t *)"1 + 1 = 2", 0);

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_track, 2, 2);
  mustHaveNonExisting(h, &metadata->backup_history[0]);
  mustHaveDirectoryCached(h, &metadata->backup_history[2]);
  PathNode *h_1 = findSubnode(h, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(h_1, &metadata->backup_history[2]);
  PathNode *h_2 = findSubnode(h_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_2, &metadata->backup_history[2], 5, (uint8_t *)".....", 0);
  PathNode *h_3 = findSubnode(h, "3", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(h_3, &metadata->backup_history[2]);
  PathNode *h_4 = findSubnode(h_3, "4", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_4, &metadata->backup_history[2], 11, (uint8_t *)"%%%%%%%%%%%", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Copy counterpart to policyChangeFromNoneInit(). */
static void policyChangeFromCopyInit(String cwd_path, size_t cwd_depth,
                                     SearchNode *change_from_copy_init)
{
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/c");
  makeDir("tmp/files/d");
  makeDir("tmp/files/e");
  makeDir("tmp/files/f");
  makeDir("tmp/files/g");
  makeDir("tmp/files/g/1");
  makeDir("tmp/files/g/1/2");
  makeDir("tmp/files/i");
  makeDir("tmp/files/i/1");
  makeDir("tmp/files/j");
  makeDir("tmp/files/l");
  makeDir("tmp/files/n");
  makeDir("tmp/files/o");
  makeDir("tmp/files/q");
  makeDir("tmp/files/r");
  makeDir("tmp/files/s");
  makeDir("tmp/files/s/2");
  generateFile("tmp/files/a/1",   "file a content", 1);
  generateFile("tmp/files/b",     "CONTENT",        1);
  generateFile("tmp/files/c/1",   "foo",            1);
  generateFile("tmp/files/e/1",   "nano backup",    1);
  generateFile("tmp/files/f/1",   "BackupBackup",   1);
  generateFile("tmp/files/f/2",   "Lorem Ipsum",    1);
  generateFile("tmp/files/j/1",   "random string",  1);
  generateFile("tmp/files/k",     "another string", 1);
  generateFile("tmp/files/l/1",   "abc",            1);
  generateFile("tmp/files/l/2",   "xyz",            1);
  generateFile("tmp/files/l/3",   "123",            1);
  generateFile("tmp/files/m",     "",               0);
  generateFile("tmp/files/n/1",   "[]",             3);
  generateFile("tmp/files/o/1",   "=",              12);
  generateFile("tmp/files/p",     "FILE_CONTENT",   1);
  generateFile("tmp/files/q/1",   "_CONTENT_",      1);
  generateFile("tmp/files/q/2",   "_FILE_",         1);
  generateFile("tmp/files/r/1",   "!@#$%^&*()_+",   1);
  generateFile("tmp/files/r/2",   "_backup_",       1);
  generateFile("tmp/files/s/1",   "abcdefghijkl",   1);
  generateFile("tmp/files/s/2/3", "ABCDEF",         1);
  makeSymlink("/dev/null", "tmp/files/h");

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_from_copy_init);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 42);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 42);

  /* Populate stat cache. */
  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 19);

  PathNode *c = findSubnode(files, "c", BH_added, BPOL_copy, 1, 1);
  cachedStat(c->path, sStat);
  cachedStat(findSubnode(c, "1", BH_added, BPOL_copy, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "d", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *f = findSubnode(files, "f", BH_added, BPOL_copy, 1, 2);
  cachedStat(f->path, sStat);
  cachedStat(findSubnode(f, "1", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(f, "2", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *j = findSubnode(files, "j", BH_added, BPOL_copy, 1, 1);
  cachedStat(j->path, sStat);
  cachedStat(findSubnode(j, "1", BH_added, BPOL_copy, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "k", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *l = findSubnode(files, "l", BH_added, BPOL_copy, 1, 3);
  cachedStat(l->path, sStat);
  cachedStat(findSubnode(l, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);
  cachedStat(findSubnode(l, "2", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(l, "3", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *o = findSubnode(files, "o", BH_added, BPOL_copy, 1, 1);
  cachedStat(o->path, sStat);
  cachedStat(findSubnode(o, "1", BH_added, BPOL_copy, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "p", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *r = findSubnode(files, "r", BH_added, BPOL_copy, 1, 2);
  cachedStat(r->path, sStat);
  cachedStat(findSubnode(r, "1", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(r, "2", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *s = findSubnode(files, "s", BH_added, BPOL_copy, 1, 2);
  cachedStat(s->path, sStat);
  cachedStat(findSubnode(s, "1", BH_added, BPOL_track, 1, 0)->path, sStat);
  PathNode *s_2 = findSubnode(s, "2", BH_added, BPOL_copy, 1, 1);
  cachedStat(s_2->path, sStat);
  cachedStat(findSubnode(s_2, "3", BH_added, BPOL_track, 1, 0)->path, sStat);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove some files. */
  removePath("tmp/files/c/1");
  removePath("tmp/files/c");
  removePath("tmp/files/d");
  removePath("tmp/files/f/2");
  removePath("tmp/files/f/1");
  removePath("tmp/files/f");
  removePath("tmp/files/j/1");
  removePath("tmp/files/j");
  removePath("tmp/files/k");
  removePath("tmp/files/l/3");
  removePath("tmp/files/l/2");
  removePath("tmp/files/l/1");
  removePath("tmp/files/l");
  removePath("tmp/files/p");
  removePath("tmp/files/r/2");
  removePath("tmp/files/r/1");
  removePath("tmp/files/r");

  /* Initiate another backup. */
  metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 42);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 42);
  initiateBackup(metadata, change_from_copy_init);

  /* Check the other backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 42);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 40);

  /* Finish the other backup. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Copy counterpart to policyChangeFromNoneChange(). */
static void policyChangeFromCopyChange(String cwd_path, size_t cwd_depth,
                                       SearchNode *change_from_copy_final)
{
  /* Remove various files directly before the initiation. */
  removePath("tmp/files/o/1");
  removePath("tmp/files/o");
  removePath("tmp/files/s/2/3");
  removePath("tmp/files/s/2");
  removePath("tmp/files/s/1");
  removePath("tmp/files/s");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 42);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 40);
  initiateBackup(metadata, change_from_copy_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 9);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 32);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 29);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 19);

  PathNode *a = findSubnode(files, "a", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *a_1 = findSubnode(a, "1", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveRegularStat(a_1, &metadata->current_backup, 14, (uint8_t *)"file a content", 0);

  PathNode *b = findSubnode(files, "b", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveRegularStat(b, &metadata->current_backup, 7, (uint8_t *)"CONTENT", 0);

  PathNode *c = findSubnode(files, "c", BH_removed | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(c_1, &metadata->backup_history[1], 3, (uint8_t *)"foo", 0);

  PathNode *d = findSubnode(files, "d", BH_removed | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);

  PathNode *e = findSubnode(files, "e", BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[1], 11, (uint8_t *)"nano backup", 0);

  PathNode *f = findSubnode(files, "f", BH_removed | BH_policy_changed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(f, &metadata->backup_history[1]);
  PathNode *f_1 = findSubnode(f, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(f_1, &metadata->backup_history[1], 12, (uint8_t *)"BackupBackup", 0);
  PathNode *f_2 = findSubnode(f, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(f_2, &metadata->backup_history[1], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *g = findSubnode(files, "g", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g, &metadata->backup_history[1]);
  PathNode *g_1 = findSubnode(g, "1", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g_1, &metadata->backup_history[1]);
  PathNode *g_2 = findSubnode(g_1, "2", BH_policy_changed, BPOL_mirror, 1, 0);
  mustHaveDirectoryStat(g_2, &metadata->backup_history[1]);

  PathNode *h = findSubnode(files, "h", BH_policy_changed, BPOL_mirror, 1, 0);
  mustHaveSymlinkLStat(h, &metadata->backup_history[1], "/dev/null");

  PathNode *i = findSubnode(files, "i", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(i, &metadata->backup_history[1]);
  PathNode *i_1 = findSubnode(i, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(i_1, &metadata->backup_history[1]);

  PathNode *j = findSubnode(files, "j", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(j, &metadata->backup_history[1]);
  PathNode *j_1 = findSubnode(j, "1", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(j_1, &metadata->backup_history[1], 13, (uint8_t *)"random string", 0);

  PathNode *k = findSubnode(files, "k", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(k, &metadata->backup_history[1], 14, (uint8_t *)"another string", 0);

  PathNode *l = findSubnode(files, "l", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 3);
  mustHaveDirectoryCached(l, &metadata->backup_history[1]);
  PathNode *l_1 = findSubnode(l, "1", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(l_1, &metadata->backup_history[1], 3, (uint8_t *)"abc", 0);
  PathNode *l_2 = findSubnode(l, "2", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(l_2, &metadata->backup_history[1], 3, (uint8_t *)"xyz", 0);
  PathNode *l_3 = findSubnode(l, "3", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(l_3, &metadata->backup_history[1], 3, (uint8_t *)"123", 0);

  PathNode *m = findSubnode(files, "m", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(m, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *n = findSubnode(files, "n", BH_policy_changed, BPOL_track, 1, 1);
  mustHaveDirectoryStat(n, &metadata->backup_history[1]);
  PathNode *n_1 = findSubnode(n, "1", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(n_1, &metadata->backup_history[1], 6, (uint8_t *)"[][][]", 0);

  PathNode *o = findSubnode(files, "o", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(o, &metadata->current_backup);
  mustHaveDirectoryCached(o, &metadata->backup_history[1]);
  PathNode *o_1 = findSubnode(o, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(o_1, &metadata->backup_history[1], 12, (uint8_t *)"============", 0);

  PathNode *p = findSubnode(files, "p", BH_removed | BH_policy_changed, BPOL_track, 2, 0);
  mustHaveNonExisting(p, &metadata->current_backup);
  mustHaveRegularCached(p, &metadata->backup_history[1], 12, (uint8_t *)"FILE_CONTENT", 0);

  PathNode *q = findSubnode(files, "q", BH_policy_changed, BPOL_track, 1, 2);
  mustHaveDirectoryStat(q, &metadata->backup_history[1]);
  PathNode *q_1 = findSubnode(q, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(q_1, &metadata->backup_history[1], 9, (uint8_t *)"_CONTENT_", 0);
  PathNode *q_2 = findSubnode(q, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(q_2, &metadata->backup_history[1], 6, (uint8_t *)"_FILE_", 0);

  PathNode *r = findSubnode(files, "r", BH_removed | BH_policy_changed, BPOL_track, 2, 2);
  mustHaveNonExisting(r, &metadata->current_backup);
  mustHaveDirectoryCached(r, &metadata->backup_history[1]);
  PathNode *r_1 = findSubnode(r, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(r_1, &metadata->current_backup);
  mustHaveRegularCached(r_1, &metadata->backup_history[1], 12, (uint8_t *)"!@#$%^&*()_+", 0);
  PathNode *r_2 = findSubnode(r, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(r_2, &metadata->backup_history[1], 8, (uint8_t *)"_backup_", 0);

  PathNode *s = findSubnode(files, "s", BH_removed | BH_policy_changed, BPOL_track, 2, 2);
  mustHaveNonExisting(s, &metadata->current_backup);
  mustHaveDirectoryCached(s, &metadata->backup_history[1]);
  PathNode *s_1 = findSubnode(s, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(s_1, &metadata->current_backup);
  mustHaveRegularCached(s_1, &metadata->backup_history[1], 12, (uint8_t *)"abcdefghijkl", 0);
  PathNode *s_2 = findSubnode(s, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(s_2, &metadata->backup_history[1]);
  PathNode *s_3 = findSubnode(s_2, "3", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(s_3, &metadata->backup_history[1], 6, (uint8_t *)"ABCDEF", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove various files to prevent rediscovering. */
  removePath("tmp/files/a/1");
  removePath("tmp/files/a");
  removePath("tmp/files/b");
}

/** Copy counterpart to policyChangeFromNonePost(). */
static void policyChangeFromCopyPost(String cwd_path, size_t cwd_depth,
                                     SearchNode *change_from_copy_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 32);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 9);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 3], 29);
  initiateBackup(metadata, change_from_copy_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 3);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 32);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 6);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 3], 29);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 14);

  PathNode *c = findSubnode(files, "c", BH_removed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(c_1, &metadata->backup_history[1], 3, (uint8_t *)"foo", 0);

  PathNode *d = findSubnode(files, "d", BH_removed, BPOL_none, 1, 0);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);

  PathNode *e = findSubnode(files, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[1], 11, (uint8_t *)"nano backup", 0);

  PathNode *f = findSubnode(files, "f", BH_removed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(f, &metadata->backup_history[1]);
  PathNode *f_1 = findSubnode(f, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(f_1, &metadata->backup_history[1], 12, (uint8_t *)"BackupBackup", 0);
  PathNode *f_2 = findSubnode(f, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(f_2, &metadata->backup_history[1], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g, &metadata->backup_history[1]);
  PathNode *g_1 = findSubnode(g, "1", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g_1, &metadata->backup_history[1]);
  PathNode *g_2 = findSubnode(g_1, "2", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveDirectoryStat(g_2, &metadata->backup_history[1]);

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveSymlinkLStat(h, &metadata->backup_history[1], "/dev/null");

  PathNode *i = findSubnode(files, "i", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(i, &metadata->backup_history[1]);
  PathNode *i_1 = findSubnode(i, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(i_1, &metadata->backup_history[1]);

  PathNode *m = findSubnode(files, "m", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(m, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *n = findSubnode(files, "n", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryStat(n, &metadata->backup_history[1]);
  PathNode *n_1 = findSubnode(n, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(n_1, &metadata->backup_history[1], 6, (uint8_t *)"[][][]", 0);

  PathNode *o = findSubnode(files, "o", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(o, &metadata->backup_history[0]);
  mustHaveDirectoryCached(o, &metadata->backup_history[1]);
  PathNode *o_1 = findSubnode(o, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(o_1, &metadata->backup_history[1], 12, (uint8_t *)"============", 0);

  PathNode *p = findSubnode(files, "p", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(p, &metadata->backup_history[0]);
  mustHaveRegularCached(p, &metadata->backup_history[1], 12, (uint8_t *)"FILE_CONTENT", 0);

  PathNode *q = findSubnode(files, "q", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(q, &metadata->backup_history[1]);
  PathNode *q_1 = findSubnode(q, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(q_1, &metadata->backup_history[1], 9, (uint8_t *)"_CONTENT_", 0);
  PathNode *q_2 = findSubnode(q, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(q_2, &metadata->backup_history[1], 6, (uint8_t *)"_FILE_", 0);

  PathNode *r = findSubnode(files, "r", BH_unchanged, BPOL_track, 2, 2);
  mustHaveNonExisting(r, &metadata->backup_history[0]);
  mustHaveDirectoryCached(r, &metadata->backup_history[1]);
  PathNode *r_1 = findSubnode(r, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(r_1, &metadata->backup_history[0]);
  mustHaveRegularCached(r_1, &metadata->backup_history[1], 12, (uint8_t *)"!@#$%^&*()_+", 0);
  PathNode *r_2 = findSubnode(r, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(r_2, &metadata->backup_history[1], 8, (uint8_t *)"_backup_", 0);

  PathNode *s = findSubnode(files, "s", BH_unchanged, BPOL_track, 2, 2);
  mustHaveNonExisting(s, &metadata->backup_history[0]);
  mustHaveDirectoryCached(s, &metadata->backup_history[1]);
  PathNode *s_1 = findSubnode(s, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(s_1, &metadata->backup_history[0]);
  mustHaveRegularCached(s_1, &metadata->backup_history[1], 12, (uint8_t *)"abcdefghijkl", 0);
  PathNode *s_2 = findSubnode(s, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(s_2, &metadata->backup_history[1]);
  PathNode *s_3 = findSubnode(s_2, "3", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(s_3, &metadata->backup_history[1], 6, (uint8_t *)"ABCDEF", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Mirror counterpart to policyChangeFromNoneInit(). */
static void policyChangeFromMirrorInit(String cwd_path, size_t cwd_depth,
                                       SearchNode *change_from_mirror_init)
{
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/a/1");
  makeDir("tmp/files/b");
  makeDir("tmp/files/c");
  makeDir("tmp/files/c/1");
  makeDir("tmp/files/e");
  makeDir("tmp/files/h");
  makeDir("tmp/files/i");
  makeDir("tmp/files/i/1");
  makeDir("tmp/files/i/3");
  makeDir("tmp/files/j");
  generateFile("tmp/files/a/1/2", "",              0);
  generateFile("tmp/files/b/1",   "random123",     1);
  generateFile("tmp/files/b/2",   "Foo-Barbar",    1);
  generateFile("tmp/files/c/1/2", "987654321",     1);
  generateFile("tmp/files/d",     "some text",     1);
  generateFile("tmp/files/e/1",   "tmp/files/e/1", 1);
  generateFile("tmp/files/f",     "... Files_e_1", 1);
  generateFile("tmp/files/g",     "",              0);
  generateFile("tmp/files/h/1",   "0",             4);
  generateFile("tmp/files/i/1/2", "x",             20);
  generateFile("tmp/files/i/2",   "%",             10);
  generateFile("tmp/files/i/3/1", "insert text",   1);
  generateFile("tmp/files/j/1",   "void",          1);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_from_mirror_init);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 26);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 26);

  /* Populate stat cache. */
  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 10);

  PathNode *b = findSubnode(files, "b", BH_added, BPOL_mirror, 1, 2);
  cachedStat(b->path, sStat);
  cachedStat(findSubnode(b, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);
  cachedStat(findSubnode(b, "2", BH_added, BPOL_track, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "d", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *e = findSubnode(files, "e", BH_added, BPOL_mirror, 1, 1);
  cachedStat(e->path, sStat);
  cachedStat(findSubnode(e, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "g", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *i = findSubnode(files, "i", BH_added, BPOL_mirror, 1, 3);
  cachedStat(i->path, sStat);
  PathNode *i_1 = findSubnode(i, "1", BH_added, BPOL_copy, 1, 1);
  cachedStat(i_1->path, sStat);
  cachedStat(findSubnode(i_1, "2", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(i, "2", BH_added, BPOL_mirror, 1, 0)->path, sStat);
  PathNode *i_3 = findSubnode(i, "3", BH_added, BPOL_track, 1, 1);
  cachedStat(i_3->path, sStat);
  cachedStat(findSubnode(i_3, "1", BH_added, BPOL_track, 1, 0)->path, sStat);

  PathNode *j = findSubnode(files, "j", BH_added, BPOL_mirror, 1, 1);
  cachedStat(j->path, sStat);
  cachedStat(findSubnode(j, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Mirror counterpart to policyChangeFromNoneChange(). */
static void policyChangeFromMirrorChange(String cwd_path, size_t cwd_depth,
                                         SearchNode *change_from_mirror_final)
{
  /* Remove various files directly before the initiation. */
  removePath("tmp/files/b/2");
  removePath("tmp/files/b/1");
  removePath("tmp/files/b");
  removePath("tmp/files/d");
  removePath("tmp/files/e/1");
  removePath("tmp/files/e");
  removePath("tmp/files/g");
  removePath("tmp/files/i/1/2");
  removePath("tmp/files/i/1");
  removePath("tmp/files/i/2");
  removePath("tmp/files/i/3/1");
  removePath("tmp/files/i/3");
  removePath("tmp/files/i");
  removePath("tmp/files/j/1");
  removePath("tmp/files/j");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 26);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 26);
  initiateBackup(metadata, change_from_mirror_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 7);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth + 23);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 21);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 10);

  PathNode *a = findSubnode(files, "a", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *a_1 = findSubnode(a, "1", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(a_1, &metadata->current_backup);
  PathNode *a_2 = findSubnode(a_1, "2", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveRegularStat(a_2, &metadata->current_backup, 0, NULL, 0);

  PathNode *b = findSubnode(files, "b", BH_removed | BH_policy_changed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(b, &metadata->backup_history[0]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[0], 9, (uint8_t *)"random123", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2, &metadata->backup_history[0], 10, (uint8_t *)"Foo-Barbar", 0);

  PathNode *c = findSubnode(files, "c", BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[0]);
  PathNode *c_1 = findSubnode(c, "1", BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c_1, &metadata->backup_history[0]);
  PathNode *c_2 = findSubnode(c_1, "2", BH_policy_changed, BPOL_copy, 1, 0);
  mustHaveRegularStat(c_2, &metadata->backup_history[0], 9, (uint8_t *)"987654321", 0);

  PathNode *d = findSubnode(files, "d", BH_removed | BH_policy_changed, BPOL_copy, 1, 0);
  mustHaveRegularCached(d, &metadata->backup_history[0], 9, (uint8_t *)"some text", 0);

  PathNode *e = findSubnode(files, "e", BH_removed | BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(e, &metadata->backup_history[0]);
  PathNode *e_1 = findSubnode(e, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(e_1, &metadata->backup_history[0], 13, (uint8_t *)"tmp/files/e/1", 0);

  PathNode *f = findSubnode(files, "f", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(f, &metadata->backup_history[0], 13, (uint8_t *)"... Files_e_1", 0);

  PathNode *g = findSubnode(files, "g", BH_removed | BH_policy_changed, BPOL_track, 2, 0);
  mustHaveNonExisting(g, &metadata->current_backup);
  mustHaveRegularCached(g, &metadata->backup_history[0], 0, NULL, 0);

  PathNode *h = findSubnode(files, "h", BH_policy_changed, BPOL_track, 1, 1);
  mustHaveDirectoryStat(h, &metadata->backup_history[0]);
  PathNode *h_1 = findSubnode(h, "1", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(h_1, &metadata->backup_history[0], 4, (uint8_t *)"0000", 0);

  PathNode *i = findSubnode(files, "i", BH_removed | BH_policy_changed, BPOL_track, 2, 3);
  mustHaveNonExisting(i, &metadata->current_backup);
  mustHaveDirectoryCached(i, &metadata->backup_history[0]);
  PathNode *i_1 = findSubnode(i, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(i_1, &metadata->backup_history[0]);
  PathNode *i_1_2 = findSubnode(i_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(i_1_2, &metadata->backup_history[0], 20, (uint8_t *)"xxxxxxxxxxxxxxxxxxxx", 0);
  PathNode *i_2 = findSubnode(i, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(i_2, &metadata->backup_history[0], 10, (uint8_t *)"%%%%%%%%%%", 0);
  PathNode *i_3 = findSubnode(i, "3", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(i_3, &metadata->current_backup);
  mustHaveDirectoryCached(i_3, &metadata->backup_history[0]);
  PathNode *i_3_1 = findSubnode(i_3, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(i_3_1, &metadata->current_backup);
  mustHaveRegularCached(i_3_1, &metadata->backup_history[0], 11, (uint8_t *)"insert text", 0);

  PathNode *j = findSubnode(files, "j", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(j, &metadata->current_backup);
  mustHaveDirectoryCached(j, &metadata->backup_history[0]);
  PathNode *j_1 = findSubnode(j, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(j_1, &metadata->backup_history[0], 4, (uint8_t *)"void", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove various files to prevent rediscovering. */
  removePath("tmp/files/a/1/2");
  removePath("tmp/files/a/1");
  removePath("tmp/files/a");
}

/** Mirror counterpart to policyChangeFromNonePost(). */
static void policyChangeFromMirrorPost(String cwd_path, size_t cwd_depth,
                                       SearchNode *change_from_mirror_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth + 23);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], cwd_depth + 7);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 21);
  initiateBackup(metadata, change_from_mirror_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 2);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth + 23);
  checkHistPoint(metadata, 0, 0, phase_timestamps[backup_counter - 1], 5);
  checkHistPoint(metadata, 1, 1, phase_timestamps[backup_counter - 2], 21);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_unchanged, 9);

  PathNode *b = findSubnode(files, "b", BH_removed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(b, &metadata->backup_history[1]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[1], 9, (uint8_t *)"random123", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2, &metadata->backup_history[1], 10, (uint8_t *)"Foo-Barbar", 0);

  PathNode *c = findSubnode(files, "c", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c_1, &metadata->backup_history[1]);
  PathNode *c_2 = findSubnode(c_1, "2", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(c_2, &metadata->backup_history[1], 9, (uint8_t *)"987654321", 0);

  PathNode *d = findSubnode(files, "d", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(d, &metadata->backup_history[1], 9, (uint8_t *)"some text", 0);

  PathNode *e = findSubnode(files, "e", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(e, &metadata->backup_history[1]);
  PathNode *e_1 = findSubnode(e, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(e_1, &metadata->backup_history[1], 13, (uint8_t *)"tmp/files/e/1", 0);

  PathNode *f = findSubnode(files, "f", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(f, &metadata->backup_history[1], 13, (uint8_t *)"... Files_e_1", 0);

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(g, &metadata->backup_history[0]);
  mustHaveRegularCached(g, &metadata->backup_history[1], 0, NULL, 0);

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryStat(h, &metadata->backup_history[1]);
  PathNode *h_1 = findSubnode(h, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(h_1, &metadata->backup_history[1], 4, (uint8_t *)"0000", 0);

  PathNode *i = findSubnode(files, "i", BH_unchanged, BPOL_track, 2, 3);
  mustHaveNonExisting(i, &metadata->backup_history[0]);
  mustHaveDirectoryCached(i, &metadata->backup_history[1]);
  PathNode *i_1 = findSubnode(i, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(i_1, &metadata->backup_history[1]);
  PathNode *i_1_2 = findSubnode(i_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(i_1_2, &metadata->backup_history[1], 20, (uint8_t *)"xxxxxxxxxxxxxxxxxxxx", 0);
  PathNode *i_2 = findSubnode(i, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(i_2, &metadata->backup_history[1], 10, (uint8_t *)"%%%%%%%%%%", 0);
  PathNode *i_3 = findSubnode(i, "3", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(i_3, &metadata->backup_history[0]);
  mustHaveDirectoryCached(i_3, &metadata->backup_history[1]);
  PathNode *i_3_1 = findSubnode(i_3, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(i_3_1, &metadata->backup_history[0]);
  mustHaveRegularCached(i_3_1, &metadata->backup_history[1], 11, (uint8_t *)"insert text", 0);

  PathNode *j = findSubnode(files, "j", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(j, &metadata->backup_history[0]);
  mustHaveDirectoryCached(j, &metadata->backup_history[1]);
  PathNode *j_1 = findSubnode(j, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(j_1, &metadata->backup_history[1], 4, (uint8_t *)"void", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Tests the handling of hash collisions. */
static void runPhaseCollision(String cwd_path, size_t cwd_depth,
                              SearchNode *phase_collision_node)
{
  /* Generate various dummy files. */
  assertTmpIsCleared();
  makeDir("tmp/files/dir");
  makeDir("tmp/files/dir/a");
  makeDir("tmp/files/backup");
  generateFile("tmp/files/dir/foo.txt",      "0",     27850);
  generateFile("tmp/files/dir/bar.txt",      "ab",    1003);
  generateFile("tmp/files/dir/a/1",          "@",     297);
  generateFile("tmp/files/dir/a/2",          "ab",    1003);
  generateFile("tmp/files/dir/a/test",       "???\n", 20);
  generateFile("tmp/files/backup/important", "ab",    1003);
  generateFile("tmp/files/backup/nano",      "%",     1572);

  const uint8_t hash_1[] =
  {
    0x0d, 0x83, 0x17, 0x31, 0x73, 0x95, 0xe7, 0x71, 0xeb, 0xa0,
    0xdd, 0xb7, 0xfb, 0xb3, 0xec, 0xf0, 0xb6, 0x1c, 0x56, 0x2e,
  };
  const uint8_t hash_3[] =
  {
    0xcc, 0x90, 0x70, 0xc2, 0x38, 0xf7, 0x4f, 0x58, 0xb4, 0xc7,
    0x6d, 0x79, 0x1f, 0x19, 0x9c, 0xb8, 0xa9, 0xae, 0x83, 0xe8,
  };
  const uint8_t hash_19[] =
  {
    0x13, 0xa9, 0xd1, 0x6d, 0xec, 0xb2, 0x5b, 0xc1, 0xa8, 0x14,
    0x23, 0x91, 0xf0, 0x94, 0x7a, 0xd3, 0x4a, 0xc4, 0xb9, 0xd6,
  };
  const uint8_t hash_255[] =
  {
    0x1f, 0xd8, 0x4a, 0xc5, 0xa2, 0x87, 0x7e, 0x7b, 0xa9, 0x59,
    0xaf, 0x33, 0x91, 0xc9, 0x5e, 0xa4, 0xee, 0x81, 0xf7, 0x9a,
  };
  const uint8_t hash_test[] =
  {
    0x14, 0xd1, 0xa2, 0x08, 0x35, 0x1d, 0xc7, 0x1c, 0x2d, 0x56,
    0x8d, 0x8f, 0xc5, 0x11, 0x06, 0x60, 0xcd, 0xca, 0x7c, 0xa5,
  };

  generateCollidingFiles(hash_1,   27850, 1);
  generateCollidingFiles(hash_3,   2006,  3);
  generateCollidingFiles(hash_19,  297,   19);
  generateCollidingFiles(hash_255, 1572,  255);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, phase_collision_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 12);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 12);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 2);

  PathNode *dir = findSubnode(files, "dir", BH_added, BPOL_copy, 1, 3);
  mustHaveDirectoryStat(dir, &metadata->current_backup);
  PathNode *foo = findSubnode(dir, "foo.txt", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(foo, &metadata->current_backup, 27850, NULL, 0);
  PathNode *bar = findSubnode(dir, "bar.txt", BH_added, BPOL_copy, 1, 0);
  mustHaveRegularStat(bar, &metadata->current_backup, 2006, NULL, 0);
  PathNode *a = findSubnode(dir, "a", BH_added, BPOL_track, 1, 3);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *a_1 = findSubnode(a, "1", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(a_1, &metadata->current_backup, 297, NULL, 0);
  PathNode *a_2 = findSubnode(a, "2", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(a_2, &metadata->current_backup, 2006, NULL, 0);
  PathNode *test = findSubnode(a, "test", BH_added, BPOL_track, 1, 0);
  mustHaveRegularStat(test, &metadata->current_backup, 80, NULL, 0);

  PathNode *backup = findSubnode(files, "backup", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryStat(backup, &metadata->current_backup);
  PathNode *important = findSubnode(backup, "important", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(important, &metadata->current_backup, 2006, NULL, 0);
  PathNode *nano = findSubnode(backup, "nano", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(nano, &metadata->current_backup, 1572, NULL, 0);

  /* Finish backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 292);
  mustHaveRegularStat(foo,       &metadata->current_backup, 27850, hash_1,    1);
  mustHaveRegularStat(bar,       &metadata->current_backup, 2006,  hash_3,    3);
  mustHaveRegularStat(a_1,       &metadata->current_backup, 297,   hash_19,   19);
  mustHaveRegularStat(a_2,       &metadata->current_backup, 2006,  hash_3,    3);
  mustHaveRegularStat(test,      &metadata->current_backup, 80,    hash_test, 0);
  mustHaveRegularStat(important, &metadata->current_backup, 2006,  hash_3,    3);
  mustHaveRegularStat(nano,      &metadata->current_backup, 1572,  hash_255,  255);
}

/** Tests the handling of a hash collision slot overflow. */
static void runPhaseSlotOverflow(String cwd_path, size_t cwd_depth,
                                 SearchNode *phase_collision_node)
{
  /* Generate various files. */
  assertTmpIsCleared();
  makeDir("tmp/files/backup");
  makeDir("tmp/files/backup/a");
  generateFile("tmp/files/backup/test", "x",  39);
  generateFile("tmp/files/backup/a/b",  "[]", 107);

  const uint8_t hash_256[] =
  {
    0x38, 0x36, 0xaa, 0x06, 0x87, 0xa0, 0x67, 0xef, 0x4e, 0x38,
    0x99, 0x3f, 0x97, 0x0d, 0x19, 0x90, 0x63, 0xb5, 0x9b, 0xfd,
  };

  generateCollidingFiles(hash_256, 214, 256);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, phase_collision_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth + 6);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth + 6);

  PathNode *files = findFilesNode(metadata, cwd_path, BH_added, 1);
  PathNode *backup = findSubnode(files, "backup", BH_added, BPOL_mirror, 1, 2);
  mustHaveDirectoryStat(backup, &metadata->current_backup);
  PathNode *test = findSubnode(backup, "test", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(test, &metadata->current_backup, 39, NULL, 0);
  PathNode *a = findSubnode(backup, "a", BH_added, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_added, BPOL_mirror, 1, 0);
  mustHaveRegularStat(b, &metadata->current_backup, 214, NULL, 0);

  /* Finish backup. */
  assert_error(finishBackup(metadata, "tmp/repo", "tmp/repo/tmp-file"),
               "overflow calculating slot number");
}

/** Runs a backup phase.

  @param test_name The name/description of the phase.
  @param phase_fun The function to run.
  @param search_tree The search tree which should be passed to the test
  function.
  @param cwd_path The path to the current working directory.
  @param cwd_depth The recursion depth of the current working directory.
*/
static void phase(const char *test_name,
                  void (*phase_fun)(String, size_t, SearchNode *),
                  SearchNode *search_tree, String cwd_path,
                  size_t cwd_depth)
{
  testGroupStart(test_name);
  phase_fun(cwd_path, cwd_depth, search_tree);
  testGroupEnd();
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
  SearchNode *phase_6_node = searchTreeLoad("generated-config-files/backup-phase-6.txt");
  SearchNode *phase_7_node = searchTreeLoad("generated-config-files/backup-phase-7.txt");
  SearchNode *phase_8_node = searchTreeLoad("generated-config-files/backup-phase-8.txt");
  SearchNode *phase_9_node = searchTreeLoad("generated-config-files/backup-phase-9.txt");
  SearchNode *phase_13_node = searchTreeLoad("generated-config-files/backup-phase-13.txt");
  SearchNode *phase_14_node = searchTreeLoad("generated-config-files/backup-phase-14.txt");
  SearchNode *phase_17_node = searchTreeLoad("generated-config-files/backup-phase-17.txt");

  SearchNode *copy_detection_node   = searchTreeLoad("generated-config-files/change-detection-copy.txt");
  SearchNode *mirror_detection_node = searchTreeLoad("generated-config-files/change-detection-mirror.txt");
  SearchNode *track_detection_node  = searchTreeLoad("generated-config-files/change-detection-track.txt");
  SearchNode *phase_collision_node  = searchTreeLoad("generated-config-files/backup-phase-collision.txt");
  SearchNode *none_filetype_node    = searchTreeLoad("generated-config-files/filetype-changes-none.txt");
  SearchNode *copy_filetype_node    = searchTreeLoad("generated-config-files/filetype-changes-copy.txt");
  SearchNode *mirror_filetype_node  = searchTreeLoad("generated-config-files/filetype-changes-mirror.txt");
  SearchNode *track_filetype_node   = searchTreeLoad("generated-config-files/filetype-changes-track.txt");

  SearchNode *change_from_none_init  = searchTreeLoad("generated-config-files/policy-change-from-none-init.txt");
  SearchNode *change_from_none_final = searchTreeLoad("generated-config-files/policy-change-from-none-final.txt");
  SearchNode *change_from_copy_init  = searchTreeLoad("generated-config-files/policy-change-from-copy-init.txt");
  SearchNode *change_from_copy_final = searchTreeLoad("generated-config-files/policy-change-from-copy-final.txt");
  SearchNode *change_from_mirror_init  = searchTreeLoad("generated-config-files/policy-change-from-mirror-init.txt");
  SearchNode *change_from_mirror_final = searchTreeLoad("generated-config-files/policy-change-from-mirror-final.txt");

  stat_cache = strTableNew();
  makeDir("tmp/repo");
  makeDir("tmp/files");
  testGroupEnd();

  phase("initial backup",                                    runPhase1,  phase_1_node, cwd, cwd_depth);
  phase("discovering new files",                             runPhase2,  phase_1_node, cwd, cwd_depth);
  phase("removing files",                                    runPhase3,  phase_3_node, cwd, cwd_depth);
  phase("backup with no changes",                            runPhase4,  phase_4_node, cwd, cwd_depth);
  phase("generating nested files and directories",           runPhase5,  phase_5_node, cwd, cwd_depth);
  phase("recursive wiping of path nodes",                    runPhase6,  phase_6_node, cwd, cwd_depth);
  phase("generate more nested files",                        runPhase7,  phase_7_node, cwd, cwd_depth);
  phase("wiping of unneeded nodes",                          runPhase8,  phase_8_node, cwd, cwd_depth);
  phase("generate nested files with varying policies",       runPhase9,  phase_9_node, cwd, cwd_depth);
  phase("recursive removing of paths with varying policies", runPhase10, phase_9_node, cwd, cwd_depth);

  /* Create a backup of the current metadata. */
  time_t tmp_timestamp = sStat("tmp").st_mtime;
  metadataWrite(metadataLoad("tmp/repo/metadata"), "tmp", "tmp/tmp-file", "tmp/metadata-backup");
  sUtime("tmp", tmp_timestamp);

  /* Run some backup phases. */
  phase("backup with no changes",                        runPhase11, phase_9_node, cwd, cwd_depth);
  phase("recreating nested files with varying policies", runPhase12, phase_9_node, cwd, cwd_depth);

  /* Restore metadata from phase 10. */
  tmp_timestamp = sStat("tmp").st_mtime;
  sRename("tmp/metadata-backup", "tmp/repo/metadata");
  sUtime("tmp", tmp_timestamp);

  /* Run more backup phases. */
  phase("a variation of the previous backup", runPhase13, phase_13_node, cwd, cwd_depth);

  testGroupStart("non-recursive re-adding of copied files");
  runPhase14(cwd, cwd_depth, phase_14_node);
  runPhase15(cwd, cwd_depth, phase_14_node);
  runPhase16(cwd, cwd_depth, phase_14_node);
  testGroupEnd();

  testGroupStart("detecting changes in nodes with no policy");
  runPhase17(cwd, cwd_depth, phase_17_node);
  runPhase18(cwd, cwd_depth, phase_17_node);
  runPhase19(cwd, cwd_depth, phase_17_node);
  runPhase20(cwd, cwd_depth, phase_17_node);
  testGroupEnd();

  testGroupStart("detecting changes in copied nodes");
  initChangeDetectionTest(cwd,   cwd_depth, copy_detection_node, BPOL_copy);
  modifyChangeDetectionTest(cwd, cwd_depth, copy_detection_node, BPOL_copy);
  changeDetectionTest(cwd,       cwd_depth, copy_detection_node, BPOL_copy);
  postDetectionTest(cwd,         cwd_depth, copy_detection_node, BPOL_copy);
  testGroupEnd();

  testGroupStart("detecting changes in mirrored nodes");
  initChangeDetectionTest(cwd,   cwd_depth, mirror_detection_node, BPOL_mirror);
  modifyChangeDetectionTest(cwd, cwd_depth, mirror_detection_node, BPOL_mirror);
  changeDetectionTest(cwd,       cwd_depth, mirror_detection_node, BPOL_mirror);
  postDetectionTest(cwd,         cwd_depth, mirror_detection_node, BPOL_mirror);
  testGroupEnd();

  testGroupStart("detecting changes in tracked nodes");
  initChangeDetectionTest(cwd,   cwd_depth, track_detection_node, BPOL_track);
  modifyChangeDetectionTest(cwd, cwd_depth, track_detection_node, BPOL_track);
  trackChangeDetectionTest(cwd,  cwd_depth, track_detection_node);
  trackPostDetectionTest(cwd,    cwd_depth, track_detection_node);
  testGroupEnd();

  testGroupStart("filetype changes in nodes with no policy");
  initNoneFiletypeChange(cwd,    cwd_depth, none_filetype_node);
  change1NoneFiletypeChange(cwd, cwd_depth, none_filetype_node);
  change2NoneFiletypeChange(cwd, cwd_depth, none_filetype_node);
  postNoneFiletypeChange(cwd,    cwd_depth, none_filetype_node);
  restoreNoneFiletypeChange(cwd, cwd_depth, none_filetype_node);
  testGroupEnd();

  testGroupStart("filetype changes in copied nodes");
  initFiletypeChange(cwd,   cwd_depth, copy_filetype_node, BPOL_copy);
  modifyFiletypeChange(cwd, cwd_depth, copy_filetype_node, BPOL_copy);
  changeFiletypeChange(cwd, cwd_depth, copy_filetype_node, BPOL_copy);
  postFiletypeChange(cwd,   cwd_depth, copy_filetype_node, BPOL_copy);
  testGroupEnd();

  testGroupStart("filetype changes in mirrored nodes");
  initFiletypeChange(cwd,   cwd_depth, mirror_filetype_node, BPOL_mirror);
  modifyFiletypeChange(cwd, cwd_depth, mirror_filetype_node, BPOL_mirror);
  changeFiletypeChange(cwd, cwd_depth, mirror_filetype_node, BPOL_mirror);
  postFiletypeChange(cwd,   cwd_depth, mirror_filetype_node, BPOL_mirror);
  testGroupEnd();

  testGroupStart("filetype changes in tracked nodes");
  initFiletypeChange(cwd,      cwd_depth, track_filetype_node, BPOL_track);
  modifyFiletypeChange(cwd,    cwd_depth, track_filetype_node, BPOL_track);
  trackFiletypeChange(cwd,     cwd_depth, track_filetype_node);
  trackFiletypeChangePost(cwd, cwd_depth, track_filetype_node, 0);
  trackFiletypeChangePost(cwd, cwd_depth, track_filetype_node, 1);
  trackFiletypeChangePost(cwd, cwd_depth, track_filetype_node, 2);
  trackFiletypeChangePost(cwd, cwd_depth, track_filetype_node, 3);
  trackFiletypeChangePost(cwd, cwd_depth, track_filetype_node, 4);
  testGroupEnd();

  testGroupStart("policy change from none");
  policyChangeFromNoneInit(cwd,   cwd_depth, change_from_none_init);
  policyChangeFromNoneChange(cwd, cwd_depth, change_from_none_final);
  policyChangeFromNonePost(cwd,   cwd_depth, change_from_none_final);
  testGroupEnd();

  testGroupStart("policy change from copy");
  policyChangeFromCopyInit(cwd,   cwd_depth, change_from_copy_init);
  policyChangeFromCopyChange(cwd, cwd_depth, change_from_copy_final);
  policyChangeFromCopyPost(cwd,   cwd_depth, change_from_copy_final);
  testGroupEnd();

  testGroupStart("policy change from mirror");
  policyChangeFromMirrorInit(cwd,   cwd_depth, change_from_mirror_init);
  policyChangeFromMirrorChange(cwd, cwd_depth, change_from_mirror_final);
  policyChangeFromMirrorPost(cwd,   cwd_depth, change_from_mirror_final);
  testGroupEnd();

  /* Run special backup phases. */
  phase("file hash collision handling",     runPhaseCollision,    phase_collision_node, cwd, cwd_depth);
  phase("collision slot overflow handling", runPhaseSlotOverflow, phase_collision_node, cwd, cwd_depth);

  free(phase_timestamps);
  strTableFree(stat_cache);
}
