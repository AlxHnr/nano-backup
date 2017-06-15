/** @file
  Implements various functions shared across backup tests.
*/

#include "backup-common.h"

#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "backup.h"
#include "restore.h"
#include "memory-pool.h"
#include "test-common.h"
#include "path-builder.h"
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
PathNode *findCwdNode(Metadata *metadata, String cwd, BackupHint hint)
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
PathNode *findSubnode(PathNode *node,
                      const char *subnode_name,
                      BackupHint hint, BackupPolicy policy,
                      size_t requested_history_length,
                      size_t requested_subnode_count)
{
  String subnode_path = strAppendPath(node->path, str(subnode_name));
  return findPathNode(node->subnodes, subnode_path.str, hint, policy,
                      requested_history_length, requested_subnode_count);
}

/** Creates a backup of the given paths parent directories timestamps. */
time_t getParentTime(const char *path)
{
  return sStat(strCopy(strSplitPath(str(path)).head).str).st_mtime;
}

/** Counterpart to getParentTime(). */
void restoreParentTime(const char *path, time_t time)
{
  const char *parent_path = strCopy(strSplitPath(str(path)).head).str;
  sUtime(parent_path, time);
}

/** Safe wrapper around mkdir(). */
void makeDir(const char *path)
{
  time_t parent_time = getParentTime(path);
  sMkdir(path);
  restoreParentTime(path, parent_time);
}

/** Safe wrapper around symlink(). */
void makeSymlink(const char *target, const char *linkpath)
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
void generateFile(const char *path, const char *content,
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
void generateCollidingFiles(const uint8_t *hash, size_t size,
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
void removePath(const char *path)
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
void regenerateFile(PathNode *node, const char *content,
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
void remakeSymlink(const char *new_target, const char *linkpath)
{
  removePath(linkpath);
  makeSymlink(new_target, linkpath);
}

/** Asserts that "tmp" contains only "repo" and "files". */
void assertTmpIsCleared(void)
{
  sRemoveRecursively("tmp");
  sMkdir("tmp");
  sMkdir("tmp/repo");
  sMkdir("tmp/files");
}

/** Finds the first point in the nodes history, which is not
  PST_non_existing. */
PathHistory *findExistingHistPoint(PathNode *node)
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
void restoreRegularFile(const char *path, const RegularFileInfo *info)
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
void restoreWithTimeRecursively(PathNode *node)
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

/* Associates a file path with its stats. */
static StringTable *stat_cache = NULL;
static StringTable **stat_cache_array = NULL;
static size_t stat_cache_array_length = 0;

/** Populates the stat_cache_array with new string tables. */
static void initStatCache(void)
{
  for(size_t index = 0; index < stat_cache_array_length; index++)
  {
    stat_cache_array[index] = strTableNew();
  }

  stat_cache = stat_cache_array[0];
}

/** Frees the string tables in stat_cache_array. */
static void freeStatCache(void)
{
  for(size_t index = 0; index < stat_cache_array_length; index++)
  {
    free(stat_cache_array[index]);
  }
}

/** Selects the stat cache with the given index. */
void setStatCache(size_t index)
{
  assert_true(index < stat_cache_array_length);
  stat_cache = stat_cache_array[index];
}

/** Stats a file and caches the result for subsequent runs.

  @param path The path to the file to stat. Must contain a null-terminated
  buffer.
  @param stat_fun The stat function to use.

  @return The stats which the given path had on its first access trough
  this function.
*/
struct stat cachedStat(String path, struct stat (*stat_fun)(const char *))
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
void resetStatCache(void)
{
  freeStatCache();
  initStatCache();
}

/** Like mustHaveRegular(), but takes a stat struct instead. */
void mustHaveRegularStats(PathNode *node, const Backup *backup,
                          struct stat stats, uint64_t size,
                          const uint8_t *hash, uint8_t slot)
{
  mustHaveRegular(node, backup, stats.st_uid, stats.st_gid, stats.st_mtime,
                  stats.st_mode, size, hash, slot);
}

/** Wrapper around mustHaveRegular(), which extracts additional
  informations using sStat(). */
void mustHaveRegularStat(PathNode *node, const Backup *backup,
                         uint64_t size, const uint8_t *hash,
                         uint8_t slot)
{
  mustHaveRegularStats(node, backup, sStat(node->path.str),
                       size, hash, slot);
}

/** Cached version of mustHaveRegularStat(). */
void mustHaveRegularCached(PathNode *node, const Backup *backup,
                           uint64_t size, const uint8_t *hash,
                           uint8_t slot)
{
  mustHaveRegularStats(node, backup, cachedStat(node->path, sStat),
                       size, hash, slot);
}

/** Like mustHaveSymlinkLStat(), but takes a stat struct instead. */
void mustHaveSymlinkStats(PathNode *node, const Backup *backup,
                          struct stat stats, const char *sym_target)
{
  mustHaveSymlink(node, backup, stats.st_uid, stats.st_gid, sym_target);
}

/** Like mustHaveRegularStat(), but for mustHaveSymlink(). */
void mustHaveSymlinkLStat(PathNode *node, const Backup *backup,
                          const char *sym_target)
{
  mustHaveSymlinkStats(node, backup, sLStat(node->path.str), sym_target);
}

/** Cached version of mustHaveSymlinkLStat(). */
void mustHaveSymlinkLCached(PathNode *node, const Backup *backup,
                            const char *sym_target)
{
  mustHaveSymlinkStats(node, backup, cachedStat(node->path, sLStat),
                       sym_target);
}

/** Like mustHaveDirectory, but takes a stat struct instead. */
void mustHaveDirectoryStats(PathNode *node, const Backup *backup,
                            struct stat stats)
{
  mustHaveDirectory(node, backup, stats.st_uid, stats.st_gid,
                    stats.st_mtime, stats.st_mode);
}

/** Like mustHaveRegularStat(), but for mustHaveDirectory(). */
void mustHaveDirectoryStat(PathNode *node, const Backup *backup)
{
  mustHaveDirectoryStats(node, backup, sStat(node->path.str));
}

/** Cached version of mustHaveDirectoryStat(). */
void mustHaveDirectoryCached(PathNode *node, const Backup *backup)
{
  mustHaveDirectoryStats(node, backup, cachedStat(node->path, sStat));
}

/** Will be updated with a copy of PWD. */
static String cwd_path;
static size_t cwd_depth_count = 0;

/** Finds the node "$PWD/tmp/files".

  @param metadata The metadata containing the nodes.
  @param hint The backup hint which all nodes in the path must have.
  @param subnode_count The amount of subnodes in "files".

  @return The "files" node.
*/
PathNode *findFilesNode(Metadata *metadata,
                        BackupHint hint,
                        size_t subnode_count)
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

/** Returns "cwd_depth_count". */
size_t cwd_depth(void)
{
  return cwd_depth_count;
}

/** Contains the timestamp at which a phase finished. */
static time_t *phase_timestamp_array = NULL;
static size_t phases_completed = 0;

/** Finishes a backup and writes the given metadata struct into "tmp/repo".
  It additionally stores the backup timestamp in "phase_timestamp_array".

  @param metadata The metadata which should be used to finish the backup.
*/
void completeBackup(Metadata *metadata)
{
  size_t phase = phases_completed;
  phases_completed++;

  phase_timestamp_array =
    sRealloc(phase_timestamp_array, sizeof *phase_timestamp_array * phases_completed);

  time_t before_finishing = sTime();
  finishBackup(metadata,  "tmp/repo", "tmp/repo/tmp-file");
  time_t after_finishing = sTime();

  assert_true(metadata->current_backup.timestamp >= before_finishing);
  assert_true(metadata->current_backup.timestamp <= after_finishing);
  phase_timestamp_array[phase] = metadata->current_backup.timestamp;

  metadataWrite(metadata, "tmp/repo", "tmp/repo/tmp-file", "tmp/repo/metadata");
}

/** Returns the timestamp of the backup `index`. */
time_t phase_timestamps(size_t index)
{
  assert_true(index < phases_completed);
  return phase_timestamp_array[index];
}

/** Returns the current value of "phases_completed". */
size_t backup_counter(void)
{
  return phases_completed;
}

/** Counterpart to initBackupCommon(). */
static void freeBackupCommon(void)
{
  free(phase_timestamp_array);
  freeStatCache();
}

/** Initializes data this functions use.

  @param stat_cache_count The amount of stat caches to create.
*/
void initBackupCommon(size_t stat_cache_count)
{
  assert_true(stat_cache_count > 0);

  stat_cache_array_length = stat_cache_count;
  stat_cache_array =
    mpAlloc(sizeof *stat_cache_array * stat_cache_array_length);
  initStatCache();

  String tmp_cwd_path = getCwd();
  memcpy(&cwd_path, &tmp_cwd_path, sizeof(cwd_path));

  cwd_depth_count = 0;
  for(size_t index = 0; index < cwd_path.length; index++)
  {
    cwd_depth_count += cwd_path.str[index] == '/';
  }

  sAtexit(freeBackupCommon);
}
