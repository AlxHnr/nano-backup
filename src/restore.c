#include "restore.h"

#include <stdlib.h>

#include "backup-helpers.h"
#include "error-handling.h"
#include "safe-wrappers.h"
#include "str.h"

/** Searches the path state which the given node had during the given
  backup id. If not found, it returns NULL. If the nodes policy doesn't
  support a history, it will return its first path state. */
static const PathState *searchPathState(const PathNode *node,
                                        const size_t id)
{
  if(node->policy != BPOL_track)
  {
    return &node->history->state;
  }

  for(PathHistory *point = node->history; point != NULL;
      point = point->next)
  {
    if(point->backup->id >= id)
    {
      return &point->state;
    }
  }

  return NULL;
}

/** Wrapper around searchPathState(), which also returns NULL if the state
  was PST_non_existing at the given backup id. */
static const PathState *searchExistingPathState(const PathNode *node,
                                                const size_t id)
{
  const PathState *state = searchPathState(node, id);

  if(state != NULL && state->type == PST_non_existing)
  {
    state = NULL;
  }

  return state;
}

/** Wrapper around searchExistingPathState(), which terminates the program
  if the state doesn't exist. */
static const PathState *findExistingPathState(const PathNode *node,
                                              const size_t id)
{
  const PathState *state = searchExistingPathState(node, id);

  if(state == NULL)
  {
    die("path didn't exist at the specified time: \"%s\"",
        node->path.content);
  }

  return state;
}

/** Checks for filetype changes and updates the given nodes backup hint.

  @param node The node which hint should be updated.
  @param state The state to compare against.
  @param stats The stats of the path on the users system.
*/
static void handleFiletypeChanges(PathNode *node, const PathState *state,
                                  const struct stat stats)
{
  if(state->type == PST_regular)
  {
    if(S_ISLNK(stats.st_mode))
    {
      backupHintSet(node->hint, BH_symlink_to_regular);
    }
    else if(S_ISDIR(stats.st_mode))
    {
      backupHintSet(node->hint, BH_directory_to_regular);
    }
    else if(!S_ISREG(stats.st_mode))
    {
      backupHintSet(node->hint, BH_other_to_regular);
    }
  }
  else if(state->type == PST_symlink)
  {
    if(S_ISREG(stats.st_mode))
    {
      backupHintSet(node->hint, BH_regular_to_symlink);
    }
    else if(S_ISDIR(stats.st_mode))
    {
      backupHintSet(node->hint, BH_directory_to_symlink);
    }
    else if(!S_ISLNK(stats.st_mode))
    {
      backupHintSet(node->hint, BH_other_to_symlink);
    }
  }
  else if(state->type == PST_directory)
  {
    if(S_ISREG(stats.st_mode))
    {
      backupHintSet(node->hint, BH_regular_to_directory);
    }
    else if(S_ISLNK(stats.st_mode))
    {
      backupHintSet(node->hint, BH_symlink_to_directory);
    }
    else if(!S_ISDIR(stats.st_mode))
    {
      backupHintSet(node->hint, BH_other_to_directory);
    }
  }
}

/** Checks the nodes path for changes.

  @param node The node representing the path. Its backup hint may be
  updated by this function.
  @param state The state against which the path should be compared.
  @param could_exist True if the path in the given node should be checked
  for existence. Otherwise it will be marked as BH_added.
*/
static void checkAndHandleChanges(PathNode *node, const PathState *state,
                                  const bool could_exist)
{
  if(could_exist && sPathExists(node->path))
  {
    const struct stat stats =
      state->type == PST_symlink ? sLStat(node->path) : sStat(node->path);

    handleFiletypeChanges(node, state, stats);
    if(backupHintNoPol(node->hint) == BH_none)
    {
      PathState dummy_state = *state;
      applyNodeChanges(node, &dummy_state, stats);
    }
  }
  else
  {
    backupHintSet(node->hint, BH_added);
  }
}

/** Recursive version of checkAndHandleChanges(). It takes the following
  additional argument:

  @param id The id of the backup against which should be compared.
*/
static void checkAndHandleChangesRecursively(PathNode *node,
                                             const PathState *state,
                                             const size_t id,
                                             bool could_exist)
{
  checkAndHandleChanges(node, state, could_exist);
  if(state->type != PST_directory)
  {
    return;
  }
  else if(backupHintNoPol(node->hint) >= BH_added &&
          backupHintNoPol(node->hint) <= BH_other_to_directory)
  {
    could_exist = false;
  }

  for(PathNode *subnode = node->subnodes; subnode != NULL;
      subnode = subnode->next)
  {
    const PathState *subnode_state = searchExistingPathState(subnode, id);
    if(subnode_state != NULL)
    {
      checkAndHandleChangesRecursively(subnode, subnode_state, id,
                                       could_exist);
    }
  }
}

/** Initiates the restoring of a node in the given node list.

  @param node_list The node list containing the given path.
  @param id The backup id to which should be restored.
  @param path The path to restore.
  @param could_exist True if the path to restore could exist. See
  checkAndHandleChanges() for more informations.
*/
static void initiateRestoreRecursively(PathNode *node_list,
                                       const size_t id, String path,
                                       const bool could_exist)
{
  bool found_node = false;

  for(PathNode *node = node_list; node != NULL && found_node == false;
      node = node->next)
  {
    if(strEqual(node->path, path))
    {
      found_node = true;
      const PathState *state = findExistingPathState(node, id);
      checkAndHandleChangesRecursively(node, state, id, could_exist);
    }
    else if(strIsParentPath(node->path, path))
    {
      found_node = true;

      const PathState *state = findExistingPathState(node, id);
      if(state->type != PST_directory)
      {
        die("path was not a directory at the specified time: \"%s\"",
            node->path.content);
      }

      checkAndHandleChanges(node, state, could_exist);

      const bool subnode_could_exist = could_exist &&
        !(backupHintNoPol(node->hint) >= BH_added &&
          backupHintNoPol(node->hint) <= BH_other_to_directory);

      initiateRestoreRecursively(node->subnodes, id, path,
                                 subnode_could_exist);
    }
  }

  if(!found_node)
  {
    die("path doesn't exist in repository: \"%s\"", path.content);
  }
}

/** Initiates the restoring of the given path.

  @param metadata An uninitialized metadata struct. It should never be
  passed to this function more than once.
  @param id The backup id to which the given path should be restored.
  @param path The full, absolute path to restore. Should not end with a
  slash. An empty string represents the root directory "/".
*/
void initiateRestore(Metadata *metadata, const size_t id, String path)
{
  if(path.length == 0)
  {
    for(PathNode *node = metadata->paths; node != NULL; node = node->next)
    {
      const PathState *state = searchExistingPathState(node, id);
      if(state != NULL)
      {
        checkAndHandleChangesRecursively(node, state, id, true);
      }
    }
  }
  else
  {
    initiateRestoreRecursively(metadata->paths, id, path, true);
  }
}

/** Restores a regular file. It will not restore metadata like timestamp,
  owner and permissions.

  @param path The path to the file to restore. If the file already exists,
  it will be overwritten.
  @param info Informations about the file.
  @param repo_path The path to the repository containing the file.
*/
void restoreFile(String path, const RegularFileInfo *info,
                 String repo_path)
{
  if(info->size > FILE_HASH_SIZE)
  {
    RepoReader *reader = repoReaderOpenFile(repo_path, path, info);
    FileStream *writer = sFopenWrite(path);
    uint64_t bytes_left = info->size;
    char buffer[4096];

    while(bytes_left > 0)
    {
      const size_t bytes_to_read =
        bytes_left > sizeof(buffer) ? sizeof(buffer) : bytes_left;

      repoReaderRead(buffer, bytes_to_read, reader);
      sFwrite(buffer, bytes_to_read, writer);

      bytes_left -= bytes_to_read;
    }

    repoReaderClose(reader);
    sFclose(writer);
  }
  else
  {
    FileStream *writer = sFopenWrite(path);
    sFwrite(info->hash, info->size, writer);
    sFclose(writer);
  }
}

/** Restores a path depending on the given state.

  @param node The node representing the path to restore.
  @param state The state to which the path should be restored.
  @param repo_path The path to the repository.
*/
static void restorePath(const PathNode *node, const PathState *state,
                        String repo_path)
{
  if(state->type == PST_regular)
  {
    restoreFile(node->path, &state->metadata.reg, repo_path);
    sChown(node->path, state->uid, state->gid);
    sChmod(node->path, state->metadata.reg.mode);
    sUtime(node->path, state->metadata.reg.timestamp);
  }
  else if(state->type == PST_symlink)
  {
    sSymlink(state->metadata.sym_target, node->path);
    sLChown(node->path, state->uid, state->gid);
  }
  else if(state->type == PST_directory)
  {
    sMkdir(node->path);
    sChown(node->path, state->uid, state->gid);
    sChmod(node->path, state->metadata.dir.mode);
    sUtime(node->path, state->metadata.dir.timestamp);
  }
}

/** Recursive counterpart to finishRestore().

  @param node The node to restore.
  @param id See finishRestore().
  @param repo_path See finishRestore().

  @return True if the restoring affected the parent directories timestamp.
*/
static bool finishRestoreRecursively(const PathNode *node, const size_t id,
                                     String repo_path)
{
  bool affects_parent_timestamp = false;

  const PathState *state = searchExistingPathState(node, id);
  if(state == NULL)
  {
    return affects_parent_timestamp;
  }

  if(backupHintNoPol(node->hint) == BH_added)
  {
    restorePath(node, state, repo_path);
    affects_parent_timestamp = true;
  }
  else if(backupHintNoPol(node->hint) >= BH_regular_to_symlink &&
          backupHintNoPol(node->hint) <= BH_other_to_directory)
  {
    if(backupHintNoPol(node->hint) == BH_directory_to_regular ||
       backupHintNoPol(node->hint) == BH_directory_to_symlink)
    {
      sRemoveRecursively(node->path);
    }
    else
    {
      sRemove(node->path);
    }

    restorePath(node, state, repo_path);
    affects_parent_timestamp = true;
  }
  else if(node->policy != BPOL_none)
  {
    if(node->hint & BH_owner_changed)
    {
      if(state->type == PST_symlink)
      {
        sLChown(node->path, state->uid, state->gid);
      }
      else
      {
        sChown(node->path, state->uid, state->gid);
      }
    }
    if(node->hint & BH_permissions_changed)
    {
      if(state->type == PST_regular)
      {
        sChmod(node->path, state->metadata.reg.mode);
      }
      else if(state->type == PST_directory)
      {
        sChmod(node->path, state->metadata.dir.mode);
      }
    }

    if(node->hint & BH_content_changed)
    {
      if(state->type == PST_regular)
      {
        restoreFile(node->path, &state->metadata.reg, repo_path);
        sUtime(node->path, state->metadata.reg.timestamp);
      }
      else if(state->type == PST_symlink)
      {
        sRemove(node->path);
        restorePath(node, state, repo_path);
        affects_parent_timestamp = true;
      }
    }
    else if(node->hint & BH_timestamp_changed)
    {
      if(state->type == PST_regular)
      {
        sUtime(node->path, state->metadata.reg.timestamp);
      }
      else if(state->type == PST_directory)
      {
        sUtime(node->path, state->metadata.dir.timestamp);
      }
    }
  }

  if(state->type == PST_directory)
  {
    bool subnode_changes_timestamp = false;

    for(const PathNode *subnode = node->subnodes; subnode != NULL;
        subnode = subnode->next)
    {
      subnode_changes_timestamp |=
        finishRestoreRecursively(subnode, id, repo_path);
    }

    if(subnode_changes_timestamp && node->policy != BPOL_none)
    {
      sUtime(node->path, state->metadata.dir.timestamp);
    }
  }

  return affects_parent_timestamp;
}

/** Completes the restoring of a path.

  @param metadata Metadata initiated via initiateRestore().
  @param id The same id which was passed to initiateRestore().
  @param repo_path The path to the backup repository.
*/
void finishRestore(const Metadata *metadata, const size_t id,
                   String repo_path)
{
  for(const PathNode *node = metadata->paths; node != NULL;
      node = node->next)
  {
    finishRestoreRecursively(node, id, repo_path);
  }
}
