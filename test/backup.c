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

#include <utime.h>
#include <unistd.h>
#include <stdlib.h>

#include "test.h"
#include "metadata.h"
#include "memory-pool.h"
#include "search-tree.h"
#include "test-common.h"
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

/** Safe wrapper around utime(). */
static void sUtime(const char *path, struct utimbuf time)
{
  if(utime(path, &time) != 0)
  {
    dieErrno("failed to restore paths time: \"%s\"", path);
  }
}

/** Creates a backup of the given paths parent directories timestamps. */
static struct utimbuf getParentTime(const char *path)
{
  struct stat stats = sStat(strCopy(strSplitPath(str(path)).head).str);

  return (struct utimbuf)
  {
    .actime  = stats.st_atime,
    .modtime = stats.st_mtime,
  };
}

/** Counterpart to getParentTime(). */
static void restoreParentTime(const char *path, struct utimbuf time)
{
  const char *parent_path = strCopy(strSplitPath(str(path)).head).str;
  sUtime(parent_path, time);
}

/** Sets the modification timestamp of the given path. */
static void setTimestamp(const char *path, time_t timestamp)
{
  struct utimbuf time =
  {
    .actime  = timestamp,
    .modtime = timestamp,
  };

  sUtime(path, time);
}

/** Safe wrapper around mkdir(). */
static void makeDir(const char *path)
{
  struct utimbuf parent_time = getParentTime(path);
  if(mkdir(path, 0755) != 0)
  {
    dieErrno("failed to create directory \"%s\"", path);
  }
  restoreParentTime(path, parent_time);
}

/** Safe wrapper around symlink(). */
static void makeSymlink(const char *target, const char *linkpath)
{
  struct utimbuf parent_time = getParentTime(linkpath);
  if(symlink(target, linkpath) != 0)
  {
    dieErrno("failed to create symlink \"%s\" -> \"%s\"", linkpath, target);
  }
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

  struct utimbuf parent_time = getParentTime(path);
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

  for(size_t slot = 0; slot < files_to_create; slot++)
  {
    info.slot = (uint8_t)slot;
    RepoWriter *writer =
      repoWriterOpenFile("tmp/repo", "tmp/repo/tmp-file",
                         "generated colliding file", &info);

    const uint8_t bytes_to_write[] = { info.slot, 0 };
    size_t bytes_left = size;
    while(bytes_left >= 2)
    {
      repoWriterWrite(bytes_to_write, 2, writer);
      bytes_left -= 2;
    }
    if(bytes_left)
    {
      repoWriterWrite(bytes_to_write, 1, writer);
    }

    repoWriterClose(writer);
  }
}

/** Safe wrapper around remove(). */
static void removePath(const char *path)
{
  struct utimbuf parent_time = getParentTime(path);
  if(remove(path) != 0)
  {
    dieErrno("failed to remove \"%s\"", path);
  }
  restoreParentTime(path, parent_time);
}

/** Counterpart to generateCollidingFiles(). */
static void removeCollidingFiles(const uint8_t *hash, size_t size,
                                 size_t files_to_remove)
{
  assert_true(files_to_remove <= UINT8_MAX + 1);

  char hash_string[FILE_HASH_SIZE * 2 + 1];
  for(size_t index = 0; index < FILE_HASH_SIZE; index++)
  {
    sprintf(&hash_string[index * 2], "%02x", hash[index]);
  }

  char buffer[64];
  for(size_t slot = 0; slot < files_to_remove; slot++)
  {
    sprintf(buffer, "tmp/repo/%zu-%s-%zu", slot, hash_string, size);
    removePath(buffer);
  }
}

/** Asserts that "tmp" contains only "repo" and "files". */
static void assertTmpIsCleared(void)
{
  assert_true(countFilesInDir("tmp") == 2);
  assert_true(countFilesInDir("tmp/repo") == 0);
  assert_true(countFilesInDir("tmp/files") == 0);
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

/** like restoreRegularFile(), but for files larger than FILE_HASH_SIZE.
  This function does not restore the files modification timestamp. */
static void restoreLargeRegularFile(const char *path,
                                    const RegularFileInfo *info)
{
  RepoReader *reader = repoReaderOpenFile("tmp/repo", path, info);
  FileStream *writer = sFopenWrite(path);
  uint64_t bytes_left = info->size;
  char *buffer = sMalloc(4096);

  while(bytes_left > 0)
  {
    size_t bytes_to_read = bytes_left > 4096 ? 4096 : bytes_left;

    repoReaderRead(buffer, bytes_to_read, reader);
    sFwrite(buffer, bytes_to_read, writer);

    bytes_left -= bytes_to_read;
  }

  free(buffer);
  repoReaderClose(reader);
  sFclose(writer);
}

/** Restores a regular file with its modification timestamp.

  @param path The path to the file.
  @param info The file info of the state to which the file should be
  restored to.
*/
static void restoreRegularFile(const char *path,
                               const RegularFileInfo *info)
{
  struct utimbuf parent_time = getParentTime(path);

  if(info->size <= FILE_HASH_SIZE)
  {
    FileStream *stream = sFopenWrite(path);
    sFwrite(info->hash, info->size, stream);
    sFclose(stream);
  }
  else
  {
    restoreLargeRegularFile(path, info);
  }

  setTimestamp(path, info->timestamp);
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
        setTimestamp(node->path.str, point->state.metadata.dir.timestamp);
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
  struct stat *cache = strtableGet(stat_cache, path);
  if(cache == NULL)
  {
    cache = mpAlloc(sizeof *cache);
    *cache = stat_fun(path.str);
    strtableMap(stat_cache, path, cache);
  }

  return *cache;
}

/** Wrapper around mustHaveRegular(), which extracts additional
  informations using sStat(). */
static void mustHaveRegularStat(PathNode *node, const Backup *backup,
                                uint64_t size, const uint8_t *hash,
                                uint8_t slot)
{
  struct stat stats = sStat(node->path.str);
  mustHaveRegular(node, backup, stats.st_uid, stats.st_gid, stats.st_mtime,
                  stats.st_mode, size, hash, slot);
}

/** Cached version of mustHaveRegularStat(). */
static void mustHaveRegularCached(PathNode *node, const Backup *backup,
                                  uint64_t size, const uint8_t *hash,
                                  uint8_t slot)
{
  struct stat stats = cachedStat(node->path, sStat);
  mustHaveRegular(node, backup, stats.st_uid, stats.st_gid, stats.st_mtime,
                  stats.st_mode, size, hash, slot);
}

/** Like mustHaveRegularStat(), but for mustHaveSymlink(). */
static void mustHaveSymlinkLStat(PathNode *node, const Backup *backup,
                                  const char *sym_target)
{
  struct stat stats = sLStat(node->path.str);
  mustHaveSymlink(node, backup, stats.st_uid, stats.st_gid, sym_target);
}

/** Cached version of mustHaveSymlinkLStat(). */
static void mustHaveSymlinkLCached(PathNode *node, const Backup *backup,
                                   const char *sym_target)
{
  struct stat stats = cachedStat(node->path, sLStat);
  mustHaveSymlink(node, backup, stats.st_uid, stats.st_gid, sym_target);
}


/** Like mustHaveRegularStat(), but for mustHaveDirectory(). */
static void mustHaveDirectoryStat(PathNode *node, const Backup *backup)
{
  struct stat stats = sStat(node->path.str);
  mustHaveDirectory(node, backup, stats.st_uid, stats.st_gid,
                    stats.st_mtime, stats.st_mode);
}

/** Cached version of mustHaveDirectoryStat(). */
static void mustHaveDirectoryCached(PathNode *node, const Backup *backup)
{
  struct stat stats = cachedStat(node->path, sStat);
  mustHaveDirectory(node, backup, stats.st_uid, stats.st_gid,
                    stats.st_mtime, stats.st_mode);
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
  assert_true(countFilesInDir("tmp/repo") == 4);
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
  assert_true(countFilesInDir("tmp/repo") == 8);
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
  assert_true(countFilesInDir("tmp/repo") == 8);
}

/** Creates more nested files. */
static void runPhase7(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_7_node)
{
  /* Remove remains from previous backups. */
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
  assert_true(countFilesInDir("tmp/repo") == 8);
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
  assert_true(countFilesInDir("tmp/repo") == 8);
}

/** Generates deeply nested files with varying policies. */
static void runPhase9(String cwd_path, size_t cwd_depth,
                      SearchNode *phase_9_node)
{
  /* Remove residue from previous backups. */
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
  assert_true(countFilesInDir("tmp/repo") == 11);
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
  assert_true(countFilesInDir("tmp/repo") == 11);
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
  assert_true(countFilesInDir("tmp/repo") == 11);
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
  assert_true(countFilesInDir("tmp/repo") == 11);
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
  assert_true(metadata->total_path_count == cwd_depth + 35);
  checkHistPoint(metadata, 0, 0, phase_timestamps[9], 8);
  checkHistPoint(metadata, 1, 1, phase_timestamps[8], 26);
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
  PathNode *bin_one = findSubnode(bin, "one", BH_not_part_of_repository, BPOL_mirror, 1, 4);
  mustHaveDirectoryCached(bin_one, &metadata->backup_history[1]);
  PathNode *bin_one_a = findSubnode(bin_one, "a", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(bin_one_a, &metadata->backup_history[1], 400, three_hash, 0);
  PathNode *bin_one_b = findSubnode(bin_one, "b", BH_not_part_of_repository, BPOL_track, 1, 2);
  mustHaveDirectoryCached(bin_one_b, &metadata->backup_history[1]);
  PathNode *bin_one_1 = findSubnode(bin_one_b, "1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(bin_one_1, &metadata->backup_history[1], 5, (uint8_t *)"dummy", 0);
  PathNode *bin_one_2 = findSubnode(bin_one_b, "2", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveSymlinkLCached(bin_one_2, &metadata->backup_history[1], "/usr/share/doc");
  PathNode *bin_one_c = findSubnode(bin_one, "c", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveDirectoryCached(bin_one_c, &metadata->backup_history[1]);
  PathNode *bin_one_d = findSubnode(bin_one, "d", BH_not_part_of_repository, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(bin_one_d, &metadata->backup_history[1]);
  PathNode *bin_one_e = findSubnode(bin_one_d, "e", BH_not_part_of_repository, BPOL_mirror, 1, 0);
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
  assert_true(countFilesInDir("tmp/repo") == 12);
  mustHaveRegularStat(bin, &metadata->current_backup, 2123, bin_hash, 0);
}

/** Creates and backups various simple files with the copy policy. */
static void runPhase14(String cwd_path, size_t cwd_depth,
                       SearchNode *phase_14_node)
{
  /* Remove various files remaining from previous phases. */
  phase10RemoveExtraFiles();
  removePath("tmp/files/bin");
  removePath("tmp/files/foo/bar/1.txt");
  removePath("tmp/files/foo/bar");
  removePath("tmp/files/foo/some file");
  removePath("tmp/files/foo");
  removePath("tmp/repo/0-2b85a2b06e498c7b976da4ff8d34ed84cb42c7e0-42");
  removePath("tmp/repo/0-46bc4f204ce9d0cd59b429b3807b6494fe77f5fe-400");
  removePath("tmp/repo/0-5571584deb0a98dcbda15dc9da9ffe1001e2b5fe-24");
  removePath("tmp/repo/0-5f0cd39ef362dc1fe6d94fbb7fec8b9fb7861054-84");
  removePath("tmp/repo/0-6c88db41c1b2b26aa7a8d5d94abdf20b3976d961-2123");
  removePath("tmp/repo/0-71e61482bfd593014183a25e6602a90f8dbc740f-56");
  removePath("tmp/repo/0-af07ccfef55c44947b630f58e82ab042ca6894b8-144");
  removePath("tmp/repo/0-b744398d179e9d86393c3349ce2406674189bb89-2100");
  removePath("tmp/repo/0-cf71d992f969b21d31940646dc6e5de6d4af2fa1-21");
  removePath("tmp/repo/0-d826d391c7dc38d37f73796168e5581f7b9982d3-1200");
  removePath("tmp/repo/0-e8fb29619700e5b60930886e94822c66ce2ad6bf-1200");
  removePath("tmp/repo/metadata");
  assertTmpIsCleared();

  /* Generate various files. */
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
  assert_true(countFilesInDir("tmp/repo") == 1);
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
  assert_true(countFilesInDir("tmp/repo") == 1);
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
  assert_true(countFilesInDir("tmp/repo") == 1);

  /* Clean up test directory. */
  phase15RemoveFiles();
  removePath("tmp/files/d");
  removePath("tmp/repo/metadata");
  assertTmpIsCleared();
}

/** Tests the handling of hash collisions. */
static void runPhaseCollision(String cwd_path, size_t cwd_depth,
                              SearchNode *phase_collision_node)
{
  /* Generate various dummy files. */
  makeDir("tmp/files/dir");
  makeDir("tmp/files/dir/a");
  makeDir("tmp/files/backup");
  generateFile("tmp/files/dir/foo.txt",      "0",     4007);
  generateFile("tmp/files/dir/bar.txt",      "ab",    1003);
  generateFile("tmp/files/dir/a/1",          "@",     297);
  generateFile("tmp/files/dir/a/2",          "ab",    1003);
  generateFile("tmp/files/dir/a/test",       "???\n", 20);
  generateFile("tmp/files/backup/important", "ab",    1003);
  generateFile("tmp/files/backup/nano",      "%",     1572);

  const uint8_t hash_1[] =
  {
    0xef, 0x07, 0xd2, 0x3d, 0xae, 0x32, 0xbd, 0xb6, 0xbd, 0x55,
    0x1b, 0x28, 0x30, 0x12, 0x3c, 0x32, 0x41, 0x6e, 0x5e, 0xa4,
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

  generateCollidingFiles(hash_1,   4007, 1);
  generateCollidingFiles(hash_3,   2006, 3);
  generateCollidingFiles(hash_19,  297,  19);
  generateCollidingFiles(hash_255, 1572, 255);

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
  mustHaveRegularStat(foo, &metadata->current_backup, 4007, NULL, 0);
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
  assert_true(countFilesInDir("tmp/repo") == 284);
  mustHaveRegularStat(foo,       &metadata->current_backup, 4007, hash_1,    1);
  mustHaveRegularStat(bar,       &metadata->current_backup, 2006, hash_3,    3);
  mustHaveRegularStat(a_1,       &metadata->current_backup, 297,  hash_19,   19);
  mustHaveRegularStat(a_2,       &metadata->current_backup, 2006, hash_3,    3);
  mustHaveRegularStat(test,      &metadata->current_backup, 80,   hash_test, 0);
  mustHaveRegularStat(important, &metadata->current_backup, 2006, hash_3,    3);
  mustHaveRegularStat(nano,      &metadata->current_backup, 1572, hash_255,  255);

  /* Clean up test. */
  removePath("tmp/files/dir/foo.txt");
  removePath("tmp/files/dir/bar.txt");
  removePath("tmp/files/dir/a/test");
  removePath("tmp/files/dir/a/2");
  removePath("tmp/files/dir/a/1");
  removePath("tmp/files/dir/a");
  removePath("tmp/files/dir");
  removePath("tmp/files/backup/nano");
  removePath("tmp/files/backup/important");
  removePath("tmp/files/backup");
  assert_true(countFilesInDir("tmp/files") == 0);

  removePath("tmp/repo/metadata");
  removePath("tmp/repo/0-14d1a208351dc71c2d568d8fc5110660cdca7ca5-80");
  removeCollidingFiles(hash_1,   4007, 2);
  removeCollidingFiles(hash_3,   2006, 4);
  removeCollidingFiles(hash_19,  297,  20);
  removeCollidingFiles(hash_255, 1572, 256);
  assert_true(countFilesInDir("tmp/repo") == 0);
  assert_true(countFilesInDir("tmp") == 2);
}

/** Tests the handling of a hash collision slot overflow. */
static void runPhaseSlotOverflow(String cwd_path, size_t cwd_depth,
                                 SearchNode *phase_collision_node)
{
  /* Generate various files. */
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

  /* Clean up generated files. */
  removePath("tmp/files/backup/test");
  removePath("tmp/files/backup/a/b");
  removePath("tmp/files/backup/a");
  removePath("tmp/files/backup");
  removeCollidingFiles(hash_256, 214, 256);

  if(sPathExists("tmp/repo/0-931293b3347b83ce52911c47277a612d7d92f99a-39"))
  {
    removePath("tmp/repo/0-931293b3347b83ce52911c47277a612d7d92f99a-39");
  }
  assertTmpIsCleared();
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

  SearchNode *phase_collision_node = searchTreeLoad("generated-config-files/backup-phase-collision.txt");

  stat_cache = strtableNew();
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
  setTimestamp("tmp", tmp_timestamp);

  /* Run some backup phases. */
  phase("backup with no changes",                        runPhase11, phase_9_node, cwd, cwd_depth);
  phase("recreating nested files with varying policies", runPhase12, phase_9_node, cwd, cwd_depth);

  /* Restore metadata from phase 10. */
  tmp_timestamp = sStat("tmp").st_mtime;
  sRename("tmp/metadata-backup", "tmp/repo/metadata");
  setTimestamp("tmp", tmp_timestamp);

  /* Run more backup phases. */
  phase("a variation of the previous backup", runPhase13, phase_13_node, cwd, cwd_depth);

  /* Reset stat table. */
  strtableFree(stat_cache);
  stat_cache = strtableNew();

  testGroupStart("non-recursive re-adding of copied files");
  runPhase14(cwd, cwd_depth, phase_14_node);
  runPhase15(cwd, cwd_depth, phase_14_node);
  runPhase16(cwd, cwd_depth, phase_14_node);
  testGroupEnd();

  /* Run special backup phases. */
  phase("file hash collision handling",     runPhaseCollision,    phase_collision_node, cwd, cwd_depth);
  phase("collision slot overflow handling", runPhaseSlotOverflow, phase_collision_node, cwd, cwd_depth);

  free(phase_timestamps);
  strtableFree(stat_cache);
}
