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
  Implements various testing functions shared across tests.
*/

#include "test-common.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** Counts the subnodes of the given node.

  @param parent_node The node containing the subnodes.

  @return The subnode count.
*/
static size_t countSubnodes(PathNode *parent_node)
{
  size_t subnode_count = 0;

  for(PathNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
  {
    subnode_count++;
  }

  return subnode_count;
}

/** Returns true if the specified regular path state contains the given
  values. */
static bool checkRegularValues(PathState *state, uint64_t size,
                               uint8_t *hash, uint8_t slot)
{
  if(state->metadata.reg.size != size)
  {
    return false;
  }
  else if(size > SHA_DIGEST_LENGTH)
  {
    return (memcmp(state->metadata.reg.hash, hash, SHA_DIGEST_LENGTH) == 0)
      && state->metadata.reg.slot == slot;
  }
  else if(size > 0)
  {
    return memcmp(state->metadata.reg.hash, hash, size) == 0;
  }
  else
  {
    return true;
  }
}

/** Performs some basic checks on the given metadatas config history.

  @param metadata The metadata struct containing the config file history.

  @return The history length of the config file.
*/
static size_t checkConfHist(Metadata *metadata)
{
  size_t history_length = 0;

  for(PathHistory *point = metadata->config_history;
      point != NULL; point = point->next)
  {
    if(point->state.type != PST_regular)
    {
      die("config history point doesn't represent a regular file");
    }
    else if(point->next != NULL &&
            point->backup->id >= point->next->backup->id)
    {
      die("config history has an invalid order");
    }

    history_length++;
  }

  return history_length;
}

/** Performs some basic checks on a path nodes history.

  @param node The node containing the history.

  @return The length of the nodes history.
*/
static size_t checkNodeHist(PathNode *node)
{
  size_t history_length = 0;

  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->next != NULL &&
       point->backup->id >= point->next->backup->id)
    {
      die("path node history has an invalid order: \"%s\"",
          node->path.str);
    }
    else if(point->state.type > PST_directory)
    {
      die("node history point has an invalid state type: \"%s\"",
          node->path.str);
    }

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
static size_t checkPathTree(PathNode *parent_node, Metadata *metadata,
                            bool check_path_table)
{
  size_t count = 0;

  for(PathNode *node = parent_node; node != NULL; node = node->next)
  {
    if(node->path.str[node->path.length] != '\0')
    {
      die("unterminated path string in metadata: \"%s\"",
          strCopy(node->path).str);
    }
    else if(check_path_table == true &&
            strtableGet(metadata->path_table, node->path) == NULL)
    {
      die("path was not mapped in metadata: \"%s\"", node->path.str);
    }
    else if(node->history == NULL)
    {
      die("path has no history: \"%s\"", node->path.str);
    }

    count += checkPathTree(node->subnodes, metadata, check_path_table);
    count++;
  }

  return count;
}

/** Determines the current working directory.

  @return A String which should not freed by the caller.
*/
String getCwd(void)
{
  size_t capacity = 128;
  char *buffer = sMalloc(capacity);
  char *result = NULL;
  int old_errno = errno;
  errno = 0;

  do
  {
    result = getcwd(buffer, capacity);
    if(result == NULL)
    {
      if(errno != ERANGE)
      {
        dieErrno("failed to get current working directory");
      }

      capacity = sSizeMul(capacity, 2);
      buffer = sRealloc(buffer, capacity);
      errno = 0;
    }
  }while(result == NULL);

  String cwd = strCopy(str(buffer));
  free(buffer);

  errno = old_errno;
  return cwd;
}

/** Performs some basic checks on a metadata struct.

  @param metadata The metadata struct to be checked.
  @param config_history_length The length of the config history which the
  given metadata must have.
  @param check_path_table True, if the associations in the metadatas path
  table should be checked.
*/
void checkMetadata(Metadata *metadata, size_t config_history_length,
                   bool check_path_table)
{
  assert_true(metadata != NULL);
  assert_true(metadata->current_backup.id == 0);
  assert_true(metadata->current_backup.timestamp == 0);

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
  assert_true(metadata->total_path_count ==
              checkPathTree(metadata->paths, metadata, check_path_table));
}

/** Assert that the given metadata contains a config history point with the
  specified properties. Counterpart to appendConfHist(). */
void mustHaveConf(Metadata *metadata, Backup *backup, uint64_t size,
                  uint8_t *hash, uint8_t slot)
{
  for(PathHistory *point = metadata->config_history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       checkRegularValues(&point->state, size, hash, slot))
    {
      return;
    }
  }

  die("config history point with id %zu doesn't exist", backup->id);
}

/** Finds a specific node in the given PathNode list. If the node couldn't
  be found, the program will be terminated with failure.

  @param start_node The beginning of the list.
  @param path_str The name of the node which should be found.
  @param policy The policy of the node.
  @param history_length The history length of the node.
  @param subnode_count The amount of subnodes.

  @return The node with the specified properties.
*/
PathNode *findNode(PathNode *start_node, const char *path_str,
                   BackupPolicy policy, size_t history_length,
                   size_t subnode_count)
{
  String path = str(path_str);
  for(PathNode *node = start_node; node != NULL; node = node->next)
  {
    if(strCompare(node->path, path) && node->policy == policy &&
       checkNodeHist(node) == history_length &&
       countSubnodes(node) == subnode_count)
    {
      return node;
    }
  }

  die("node \"%s\" with the specified properties does not exist",
      path_str);
  return NULL;
}

/** Assert that the given node has a non-existing path state at the given
  backup point. */
void mustHaveNonExisting(PathNode *node, Backup *backup)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_non_existing)
    {
      return;
    }
  }

  die("node \"%s\" has no non-existing history point at backup %zu",
      node->path.str, backup->id);
}

/** Assert that the given node contains a history point with the specified
  properties. */
void mustHaveRegular(PathNode *node, Backup *backup, uid_t uid, gid_t gid,
                     time_t timestamp, mode_t mode, uint64_t size,
                     uint8_t *hash, uint8_t slot)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_regular &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       point->state.metadata.reg.mode == mode &&
       checkRegularValues(&point->state, size, hash, slot))
    {
      return;
    }
  }

  die("path node \"%s\" has no regular path state in its history",
      node->path.str);
}

/** Assert that the given node contains a symlink history point with the
  specified properties. */
void mustHaveSymlink(PathNode *node, Backup *backup, uid_t uid, gid_t gid,
                     time_t timestamp, const char *sym_target)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_symlink &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       strcmp(point->state.metadata.sym_target, sym_target) == 0)
    {
      return;
    }
  }

  die("path node \"%s\" doesn't have the symlink \"%s\" in its history",
      node->path.str, sym_target);
}

/** Assert that the given node contains a directory history point with the
  specified properties. */
void mustHaveDirectory(PathNode *node, Backup *backup, uid_t uid,
                       gid_t gid, time_t timestamp, mode_t mode)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_directory &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       point->state.metadata.dir_mode == mode)
    {
      return;
    }
  }

  die("path node \"%s\" was not a directory at backup point %zu",
      node->path.str, backup->id);
}