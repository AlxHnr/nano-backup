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
  Implements fundamental backup operations.
*/

#include "backup.h"

#include <string.h>
#include <unistd.h>

#include "search.h"
#include "memory-pool.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** Constructs a path history point from the given data.

  @param metadata The metadata to which the point belongs to.
  @param result The search result describing a found path, which must have
  the type SRT_regular, SRT_symlink or SRT_directory. Otherwise it will
  result in undefined behaviour.

  @return A new PathHistory point that should not be freed by the caller.
*/
static PathHistory *buildPathHistoryPoint(Metadata *metadata,
                                          SearchResult result)
{
  PathHistory *point = mpAlloc(sizeof *point);

  point->backup = &metadata->current_backup;
  point->backup->ref_count = sSizeAdd(point->backup->ref_count, 1);

  point->state.uid = result.stats.st_uid;
  point->state.gid = result.stats.st_gid;
  point->state.timestamp = result.stats.st_mtime;

  if(result.type == SRT_regular)
  {
    point->state.type = PST_regular;
    point->state.metadata.reg.mode = result.stats.st_mode;
    point->state.metadata.reg.size = result.stats.st_size;
  }
  else if(result.type == SRT_symlink)
  {
    point->state.type = PST_symlink;

    char *buffer = mpAlloc(sSizeAdd(result.stats.st_size, 1));

    /* Although st_size bytes are enough to store the symlinks target path,
       +1 is added to make use of the full buffer. This allows to detect
       whether the symlink has increased in size since its last lstat() or
       not. */
    ssize_t read_bytes =
      readlink(result.path.str, buffer, result.stats.st_size + 1);

    if(read_bytes == -1)
    {
      dieErrno("failed to read symlink: \"%s\"", result.path.str);
    }
    else if(read_bytes != result.stats.st_size)
    {
      die("symlink changed while reading: \"%s\"", result.path.str);
    }

    buffer[result.stats.st_size] = '\0';

    point->state.metadata.sym_target = buffer;
  }
  else if(result.type == SRT_directory)
  {
    point->state.type = PST_directory;
    point->state.metadata.dir_mode = result.stats.st_mode;
  }

  point->next = NULL;

  return point;
}

/** Queries and processes the next search result recursively and updates
  the given metadata as described in the documentation of initiateBackup().

  @param metadata The metadata which should be updated.
  @param node_list A pointer to the node list corresponding to the
  currently traversed directory.
  @param context The context from which the search result should be
  queried.

  @return The type of the processed result.
*/
static SearchResultType initiateMetadataRecursively(Metadata *metadata,
                                                    PathNode **node_list,
                                                    SearchContext *context)
{
  SearchResult result = searchGetNext(context);
  if(result.type == SRT_end_of_directory ||
     result.type == SRT_end_of_search ||
     result.type == SRT_other)
  {
    return result.type;
  }

  PathNode *node = strtableGet(metadata->path_table, result.path);

  if(node == NULL)
  {
    node = mpAlloc(sizeof *node);

    String path_copy = strCopy(result.path);
    memcpy(&node->path, &path_copy, sizeof(node->path));

    node->policy = result.policy;
    node->history = buildPathHistoryPoint(metadata, result);
    node->subnodes = NULL;

    /* Prepend the new node to the current node list. */
    node->next = *node_list;
    *node_list = node;

    metadata->total_path_count = sSizeAdd(metadata->total_path_count, 1);
  }

  if(result.type == SRT_directory)
  {
    while(initiateMetadataRecursively(metadata, &node->subnodes, context)
          != SRT_end_of_directory);
  }

  return result.type;
}

/** Initiates a backup by updating the given metadata with new or changed
  files found trough the specified search tree. To speed things up, hash
  computations of some files are skipped, which leaves the metadata in an
  incomplete state once this function returns. This allows to show a short
  summary of changes to the user as early as possible, before continuing
  with the backup.

  @param metadata A valid metadata struct containing informations about the
  latest backup. Once this function returns, the metadata will be left in
  an incomplete state and should never be passed to this function again.
  It should not be written to disk unless the backup gets completed,
  otherwise it may lead to a corrupted backup repository.
  @param root_node The search tree which will be used to search the file
  system. This function will modify the given tree as described in the
  documentation of searchNew().
*/
void initiateBackup(Metadata *metadata, SearchNode *root_node)
{
  SearchContext *context = searchNew(root_node);
  while(initiateMetadataRecursively(metadata, &metadata->paths, context)
        != SRT_end_of_search);
}
