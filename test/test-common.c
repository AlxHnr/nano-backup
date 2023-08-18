#include "test-common.h"

#include <errno.h>
#include <ftw.h>
#include <stdlib.h>
#include <unistd.h>

#include "CRegion/global-region.h"
#include "error-handling.h"
#include "safe-wrappers.h"
#include "test.h"

static size_t countSubnodes(const PathNode *parent_node)
{
  size_t subnode_count = 0;

  for(const PathNode *node = parent_node->subnodes; node != NULL; node = node->next)
  {
    subnode_count++;
  }

  return subnode_count;
}

/** Returns true if the specified regular path state contains the given
  values. If the given hash is NULL, both the hash and the slot are not
  checked. */
static bool checkRegularValues(const PathState *state, const uint64_t size, const uint8_t *hash, uint8_t slot)
{
  if(state->metadata.file_info.size != size)
  {
    return false;
  }
  if(hash == NULL)
  {
    return true;
  }
  if(size > FILE_HASH_SIZE)
  {
    return (memcmp(state->metadata.file_info.hash, hash, FILE_HASH_SIZE) == 0) &&
      state->metadata.file_info.slot == slot;
  }
  return size == 0 || memcmp(state->metadata.file_info.hash, hash, size) == 0;
}

/** Checks if the next node in the given history point is greater. This
  can be used to determine if a history is ordered.

  @param metadata The metadata to which the given point belongs.
  @param point The point which should be compared to its follow up element,
  that can be NULL. In this case true will be returned.

  @return True, if the points follow up is in the correct order.
*/
static bool nextNodeGreater(const Metadata *metadata, const PathHistory *point)
{
  if(point->next == NULL ||
     (point->backup == &metadata->current_backup && point->next->backup != &metadata->current_backup))
  {
    return true;
  }

  return point->backup->id < point->next->backup->id;
}

/** Performs some basic checks on the given metadatas config history.

  @param metadata The metadata struct containing the config file history.

  @return The history length of the config file.
*/
static size_t checkConfHist(const Metadata *metadata)
{
  size_t history_length = 0;

  for(const PathHistory *point = metadata->config_history; point != NULL; point = point->next)
  {
    if(point->state.type != PST_regular_file)
    {
      die("config history point doesn't represent a regular file");
    }
    else if(!nextNodeGreater(metadata, point))
    {
      die("config history has an invalid order");
    }

    history_length++;
  }

  return history_length;
}

static size_t getHistoryLength(const PathNode *node)
{
  size_t history_length = 0;

  for(const PathHistory *point = node->history; point != NULL; point = point->next)
  {
    history_length++;
  }

  return history_length;
}

/** Checks a path tree recursively and terminates the program on errors.

  @param parent_node The first node in the list, which should be checked
  recursively.
  @param metadata The metadata to which the tree belongs.
  @param check_path_table True, if the associations in the metadatas path
  table should be checked.

  @return The amount of path nodes in the entire tree.
*/
static size_t checkPathTree(const PathNode *parent_node, const Metadata *metadata, const bool check_path_table)
{
  size_t count = 0;

  for(const PathNode *node = parent_node; node != NULL; node = node->next)
  {
    if(backupHintNoPol(node->hint) == BH_not_part_of_repository)
    {
      continue;
    }
    if(check_path_table && strTableGet(metadata->path_table, node->path) == NULL)
    {
      die("path was not mapped in metadata: \"" PRI_STR "\"", STR_FMT(node->path));
    }
    else if(node->history == NULL)
    {
      die("path has no history: \"" PRI_STR "\"", STR_FMT(node->path));
    }
    else
      for(PathHistory *point = node->history; point != NULL; point = point->next)
      {
        if(!nextNodeGreater(metadata, point))
        {
          die("path node history has an invalid order: \"" PRI_STR "\"", STR_FMT(node->path));
        }
        else if(point->state.type > PST_directory)
        {
          die("node history point has an invalid state type: \"" PRI_STR "\"", STR_FMT(node->path));
        }
      }

    count += checkPathTree(node->subnodes, metadata, check_path_table);
    count++;
  }

  return count;
}

/** Searches for the specified backup in the given list.

  @param start_point The lists first element.
  @param backup The Backup to search.

  @return The requested history point, or NULL.
*/
static const PathHistory *searchHistoryPoint(const PathHistory *start_point, const Backup *backup)
{
  for(const PathHistory *point = start_point; point != NULL; point = point->next)
  {
    if(point->backup == backup)
    {
      return point;
    }
  }

  return NULL;
}

/** Finds the specified backup in the given nodes history and terminates
  the program on failure.

  @param node The node containing the history to search.
  @param backup The backup node which should be found.

  @return The requested history point containing the specified backup.
*/
static const PathHistory *findHistoryPoint(const PathNode *node, const Backup *backup)
{
  const PathHistory *point = searchHistoryPoint(node->history, backup);

  if(point == NULL)
  {
    die("node \"" PRI_STR "\" doesn't have a backup with id %zu in its history", STR_FMT(node->path), backup->id);
  }

  return point;
}

/** Asserts that the node at the given history point contains the specified
  values. */
static void checkPathState(const PathNode *node, const PathHistory *point, const uid_t uid, const gid_t gid)
{
  if(point->state.uid != uid)
  {
    die("backup point %zu in node \"" PRI_STR "\" contains invalid uid", point->backup->id, STR_FMT(node->path));
  }
  else if(point->state.gid != gid)
  {
    die("backup point %zu in node \"" PRI_STR "\" contains invalid gid", point->backup->id, STR_FMT(node->path));
  }
}

static size_t directory_item_counter = 0;

/** Increments directory_item_counter and can be passed to nftw(). */
static int countItems(const char *path, const struct stat *stats, const int type, struct FTW *ftw)
{
  /* Ignore all arguments. */
  (void)path;
  (void)stats;
  (void)type;
  (void)ftw;

  directory_item_counter++;

  return 0;
}

/** Counts the items in the specified directory recursively. */
size_t countItemsInDir(const char *path)
{
  directory_item_counter = 0;

  if(nftw(path, countItems, 10, FTW_PHYS) == -1)
  {
    dieErrno("failed to count items in directory: \"%s\"", path);
  }

  /* The given directory does not count, thus - 1. */
  return directory_item_counter - 1;
}

/** Performs some basic checks on a metadata struct.

  @param metadata The metadata struct to be checked.
  @param config_history_length The length of the config history which the
  given metadata must have.
  @param check_path_table True, if the associations in the metadatas path
  table should be checked.
*/
void checkMetadata(const Metadata *metadata, const size_t config_history_length, const bool check_path_table)
{
  assert_true(metadata != NULL);
  assert_true(metadata->current_backup.id == 0);
  assert_true(metadata->current_backup.completion_time == 0);

  if(metadata->backup_history_length == 0)
  {
    assert_true(metadata->backup_history == NULL);
  }
  else
  {
    assert_true(metadata->backup_history != NULL);
  }

  assert_true(checkConfHist(metadata) == config_history_length);
  assert_true(metadata->path_table != NULL);
  assert_true(metadata->total_path_count == checkPathTree(metadata->paths, metadata, check_path_table));
}

/** Performs some checks on the metadatas backup history.

  @param metadata The metadata containing the backup history.
  @param index The index of the history point to check.
  @param id The id which the point must have.
  @param completion_time The timestamp which the point must have.
  @param ref_count The reference count which the point must have.
*/
void checkHistPoint(const Metadata *metadata, const size_t index, const size_t id, const time_t completion_time,
                    const size_t ref_count)
{
  assert_true(metadata->backup_history[index].id == id);
  assert_true(metadata->backup_history[index].completion_time == completion_time);
  assert_true(metadata->backup_history[index].ref_count == ref_count);
}

/** Assert that the given metadata contains a config history point with the
  specified properties. Counterpart to appendConfHist(). */
void mustHaveConf(const Metadata *metadata, const Backup *backup, const uint64_t size, const uint8_t *hash,
                  uint8_t slot)
{
  const PathHistory *point = searchHistoryPoint(metadata->config_history, backup);

  if(point == NULL)
  {
    die("config history has no backup with id %zu", backup->id);
  }
  else if(!checkRegularValues(&point->state, size, hash, slot))
  {
    die("config history has invalid values at id %zu", backup->id);
  }
}

/** Finds a specific node in the given PathNode list. If the node couldn't
  be found, the program will be terminated with failure.

  @param start_node The beginning of the list.
  @param path_str The name of the node which should be found.
  @param policy The policy of the node.
  @param history_length The history length of the node.
  @param subnode_count The amount of subnodes.
  @param hint The nodes backup hint.

  @return The node with the specified properties.
*/
PathNode *findPathNode(PathNode *start_node, const char *path_str, BackupHint hint, BackupPolicy policy,
                       size_t history_length, size_t subnode_count)
{
  PathNode *requested_node = NULL;

  for(PathNode *node = start_node; node != NULL && requested_node == NULL; node = node->next)
  {
    if(strIsEqual(node->path, str(path_str)))
    {
      requested_node = node;
    }
  }

  if(requested_node == NULL)
  {
    die("requested node doesn't exist: \"%s\"", path_str);
  }
  else if(requested_node->hint != hint)
  {
    die("requested node has wrong backup hint: \"%s\"", path_str);
  }
  else if(requested_node->policy != policy)
  {
    die("requested node has wrong policy: \"%s\"", path_str);
  }
  else if(getHistoryLength(requested_node) != history_length)
  {
    die("requested node has wrong history length: \"%s\"", path_str);
  }
  else if(countSubnodes(requested_node) != subnode_count)
  {
    die("requested node has wrong subnode count: \"%s\"", path_str);
  }

  return requested_node;
}

/** Assert that the given node has a non-existing path state at the given
  backup point. */
void mustHaveNonExisting(const PathNode *node, const Backup *backup)
{
  const PathHistory *point = findHistoryPoint(node, backup);
  if(point->state.type != PST_non_existing)
  {
    die("backup point %zu in node \"" PRI_STR "\" doesn't have the state PST_non_existing", backup->id,
        STR_FMT(node->path));
  }
}

/** Assert that the given node contains a history point with the specified
  properties. */
void mustHaveRegular(const PathNode *node, const Backup *backup, const uid_t uid, const gid_t gid,
                     const time_t modification_time, const mode_t permission_bits, const uint64_t size,
                     const uint8_t *hash, const uint8_t slot)
{
  const PathHistory *point = findHistoryPoint(node, backup);
  if(point->state.type != PST_regular_file)
  {
    die("backup point %zu in node \"" PRI_STR "\" doesn't have the state PST_regular", backup->id,
        STR_FMT(node->path));
  }
  else if(point->state.metadata.file_info.permission_bits != permission_bits)
  {
    die("backup point %zu in node \"" PRI_STR "\" contains invalid permission bits", backup->id,
        STR_FMT(node->path));
  }
  else if(point->state.metadata.file_info.modification_time != modification_time)
  {
    die("backup point %zu in node \"" PRI_STR "\" contains invalid modification_time", backup->id,
        STR_FMT(node->path));
  }
  else if(!checkRegularValues(&point->state, size, hash, slot))
  {
    die("backup point %zu in node \"" PRI_STR "\" contains invalid values", backup->id, STR_FMT(node->path));
  }

  checkPathState(node, point, uid, gid);
}

/** Assert that the given node contains a symlink history point with the
  specified properties. */
void mustHaveSymlink(const PathNode *node, const Backup *backup, const uid_t uid, const gid_t gid,
                     const char *symlink_target)
{
  const PathHistory *point = findHistoryPoint(node, backup);
  if(point->state.type != PST_symlink)
  {
    die("backup point %zu in node \"" PRI_STR "\" doesn't have the state PST_symlink", backup->id,
        STR_FMT(node->path));
  }
  else if(!strIsEqual(point->state.metadata.symlink_target, str(symlink_target)))
  {
    die("backup point %zu in node \"" PRI_STR "\" doesn't contain the symlink target \"%s\"", backup->id,
        STR_FMT(node->path), symlink_target);
  }

  checkPathState(node, point, uid, gid);
}

/** Assert that the given node contains a directory history point with the
  specified properties. */
void mustHaveDirectory(const PathNode *node, const Backup *backup, const uid_t uid, const gid_t gid,
                       const time_t modification_time, const mode_t permission_bits)
{
  const PathHistory *point = findHistoryPoint(node, backup);
  if(point->state.type != PST_directory)
  {
    die("backup point %zu in node \"" PRI_STR "\" doesn't have the state PST_directory", backup->id,
        STR_FMT(node->path));
  }
  else if(point->state.metadata.directory_info.permission_bits != permission_bits)
  {
    die("backup point %zu in node \"" PRI_STR "\" contains invalid permission bits", backup->id,
        STR_FMT(node->path));
  }
  else if(point->state.metadata.directory_info.modification_time != modification_time)
  {
    die("backup point %zu in node \"" PRI_STR "\" contains invalid modification_time", backup->id,
        STR_FMT(node->path));
  }

  checkPathState(node, point, uid, gid);
}

/** Returns a temporary, single-use copy of the given string which is null-terminated. */
const char *nullTerminate(StringView string)
{
  static Allocator *buffer = NULL;
  if(buffer == NULL)
  {
    buffer = allocatorWrapOneSingleGrowableBuffer(CR_GetGlobalRegion());
  }
  return strGetContent(string, buffer);
}
