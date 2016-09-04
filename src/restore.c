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
  Implements restoring of files.
*/

#include "restore.h"

#include <stdlib.h>

#include "string-utils.h"
#include "safe-wrappers.h"
#include "backup-helpers.h"
#include "error-handling.h"

/** Searches the path state which the given node had during the given
  backup id. If not found, it returns NULL. If the nodes policy doesn't
  support a history, it will return its first path state. */
static PathState *searchPathState(PathNode *node, size_t id)
{
  if(node->policy != BPOL_track)
  {
    return &node->history->state;
  }

  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
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
static PathState *searchExistingPathState(PathNode *node, size_t id)
{
  PathState *state = searchPathState(node, id);

  if(state != NULL && state->type == PST_non_existing)
  {
    state = NULL;
  }

  return state;
}

/** Wrapper around searchExistingPathState(), which terminates the program
  if the state doesn't exist. */
static PathState *findExistingPathState(PathNode *node, size_t id)
{
  PathState *state = searchExistingPathState(node, id);

  if(state == NULL)
  {
    die("path didn't exist at the specified time: \"%s\"", node->path.str);
  }

  return state;
}

/** Checks for filetype changes and updates the given nodes backup hint.

  @param node The node which hint should be updated.
  @param state The state to compare against.
  @param stats The stats of the path on the users system.
*/
static void handleFiletypeChanges(PathNode *node, PathState *state,
                                  struct stat stats)
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
    else if(S_ISREG(stats.st_mode) == false)
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
    else if(S_ISLNK(stats.st_mode) == false)
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
    else if(S_ISDIR(stats.st_mode) == false)
    {
      backupHintSet(node->hint, BH_other_to_directory);
    }
  }
}

/** Checks the nodes path for changes.

  @param node The node representing the path. Its backup hint may be
  updated by this function.
  @param state The state against which the path should be compared.
*/
static void checkAndHandleChanges(PathNode *node, PathState *state)
{
  if(sPathExists(node->path.str))
  {
    struct stat stats =
      state->type == PST_symlink?
      sLStat(node->path.str):
      sStat(node->path.str);

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
                                             PathState *state,
                                             size_t id)
{
  checkAndHandleChanges(node, state);

  for(PathNode *subnode = node->subnodes;
      subnode != NULL; subnode = subnode->next)
  {
    PathState *subnode_state = searchExistingPathState(subnode, id);
    if(subnode_state != NULL && subnode_state->type == PST_directory)
    {
      checkAndHandleChangesRecursively(subnode, subnode_state, id);
    }
  }
}

/** Initiates the restoring of a node in the given node list.

  @param node_list The node list containing the given path.
  @param id The backup id to which should be restored.
  @param path The path to restore.
*/
static void initiateRestoreRecursively(PathNode *node_list,
                                       size_t id, String path)
{
  bool found_node = false;

  for(PathNode *node = node_list;
      node != NULL && found_node == false;
      node = node->next)
  {
    if(strCompare(node->path, path))
    {
      found_node = true;
      PathState *state = findExistingPathState(node, id);
      checkAndHandleChangesRecursively(node, state, id);
    }
    else if(strIsParentPath(node->path, path))
    {
      found_node = true;
      checkAndHandleChanges(node, findExistingPathState(node, id));
      initiateRestoreRecursively(node->subnodes, id, path);
    }
  }

  if(found_node == false)
  {
    die("path doesn't exist in repository: \"%s\"", path.str);
  }
}

/** Initiates the restoring of the given path.

  @param metadata An uninitialized metadata struct. It should never be
  passed to this function more than once.
  @param id The backup id to which the given path should be restored.
  @param path_to_restore The full, absolute path to restore.
*/
void initiateRestore(Metadata *metadata, size_t id,
                     const char *path_to_restore)
{
  initiateRestoreRecursively(metadata->paths, id, str(path_to_restore));
}
