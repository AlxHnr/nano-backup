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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "search.h"
#include "file-hash.h"
#include "repository.h"
#include "memory-pool.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** Reads the content of a symlink.

  @param result The result trough which the symlink was discovered.

  @return The content of the symlink. Should not be freed by the caller.
*/
static const char *readSymlink(SearchResult result)
{
  uint64_t buffer_length = sUint64Add(result.stats.st_size, 1);
  if(buffer_length > SIZE_MAX)
  {
    die("symlink does not fit in memory: \"%s\"", result.path.str);
  }

  char *buffer = mpAlloc(buffer_length);

  /* Although st_size bytes are enough to store the symlinks target path,
     the full buffer is used. This allows to detect whether the symlink
     has increased in size since its last lstat() or not. */
  ssize_t read_bytes = readlink(result.path.str, buffer, buffer_length);

  if(read_bytes == -1)
  {
    dieErrno("failed to read symlink: \"%s\"", result.path.str);
  }
  else if(read_bytes != result.stats.st_size)
  {
    die("symlink changed while reading: \"%s\"", result.path.str);
  }

  buffer[result.stats.st_size] = '\0';

  return buffer;
}

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

  if(result.type == SRT_regular)
  {
    point->state.type = PST_regular;
    point->state.metadata.reg.mode = result.stats.st_mode;
    point->state.metadata.reg.timestamp = result.stats.st_mtime;
    point->state.metadata.reg.size = result.stats.st_size;
  }
  else if(result.type == SRT_symlink)
  {
    point->state.type = PST_symlink;
    point->state.metadata.sym_target = readSymlink(result);
  }
  else if(result.type == SRT_directory)
  {
    point->state.type = PST_directory;
    point->state.metadata.dir.mode = result.stats.st_mode;
    point->state.metadata.dir.timestamp = result.stats.st_mtime;
  }

  point->next = NULL;

  return point;
}

/** Safely reassigns the history points backup to the metadatas current
  backup.

  @param metadata The metadata containing the current backup point.
  @param point The point to update.
*/
static void reassignPointToCurrent(Metadata *metadata, PathHistory *point)
{
  point->backup->ref_count--;
  point->backup = &metadata->current_backup;
  point->backup->ref_count = sSizeAdd(point->backup->ref_count, 1);
}

/** Matches the given search node against the specified path tail.

  @param node The node containing the data used for matching.
  @param path_tail The last element of a path. This string should not
  contain any slashes and must be null-terminated.

  @return True, if the given node matches the specified path tail.
*/
static bool searchNodeMatches(SearchNode *node, String path_tail)
{
  if(node->regex)
  {
    return regexec(node->regex, path_tail.str, 0, NULL, 0) == 0;
  }
  else
  {
    return strCompare(node->name, path_tail);
  }
}

/** Checks if a subnode of the given result node matches the specified
  path.

  @param path The path to match. Must be null-terminated.
  @param result The node containing the subnodes used for matching. Can be
  NULL.

  @return The results subnode that has matched the given string, or NULL.
*/
static SearchNode *matchesSearchSubnodes(String path, SearchNode *result)
{
  if(result != NULL)
  {
    String path_tail = strSplitPath(path).tail;
    for(SearchNode *node = result->subnodes;
        node != NULL; node = node->next)
    {
      if(searchNodeMatches(node, path_tail))
      {
        return node;
      }
    }
  }

  return NULL;
}

/** Matches the given ignore expression list against the specified path.

  @param path A null-terminated path which should be matched.
  @param ignore_list A list of ignore expressions or NULL.

  @return True, if one ignore expression matched the specified path.
*/
static bool matchesIgnoreList(String path, RegexList *ignore_list)
{
  for(RegexList *item = ignore_list; item != NULL; item = item->next)
  {
    if(regexec(item->regex, path.str, 0, NULL, 0) == 0)
    {
      return true;
    }
  }

  return false;
}

/** Marks the given node as BH_not_part_of_repository and decrements all
  reference counts it causes.

  @param metadata The metadata of the current backup.
  @param node The node to process.
*/
static void prepareNodeForWiping(Metadata *metadata, PathNode *node)
{
  node->hint = BH_not_part_of_repository;
  metadata->total_path_count--;

  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    point->backup->ref_count--;
  }
}

/** Recursive version of prepareNodeForWiping(). */
static void prepareNodeForWipingRecursively(Metadata *metadata,
                                            PathNode *node)
{
  prepareNodeForWiping(metadata, node);
  for(PathNode *subnode = node->subnodes;
      subnode != NULL; subnode = subnode->next)
  {
    prepareNodeForWipingRecursively(metadata, subnode);
  }
}

/** Marks the given tree recursively as BH_removed. Tracked nodes which
  where removed at the previous backup will be marked as BH_unchanged.

  @param metadata The metadata of the current backup.
  @param node The node which should be updated recursively.
  @param extend_tracked_histories True if a tracked nodes history should be
  extended with a "removed" state. Must be true for the initial call.
*/
static void markAsRemovedRecursively(Metadata *metadata, PathNode *node,
                                     bool extend_tracked_histories)
{
  if(node->history->state.type == PST_non_existing)
  {
    node->hint = BH_unchanged;
  }
  else
  {
    node->hint = BH_removed;
    extend_tracked_histories &= (node->policy == BPOL_track);

    if(extend_tracked_histories)
    {
      PathHistory *point = mpAlloc(sizeof *point);
      point->backup = &metadata->current_backup;
      point->backup->ref_count = sSizeAdd(point->backup->ref_count, 1);
      point->state.type = PST_non_existing;
      point->next = node->history;
      node->history = point;
    }
  }

  for(PathNode *subnode = node->subnodes;
      subnode != NULL; subnode = subnode->next)
  {
    markAsRemovedRecursively(metadata, subnode, extend_tracked_histories);
  }
}

/** Handles a node, which path was removed from the users filesystem.

  @param metadata The metadata of the current backup.
  @param node The node representing the removed file.
  @param policy The policy which the removed path is supposed to have.
*/
static void handleRemovedPath(Metadata *metadata, PathNode *node,
                              BackupPolicy policy)
{
  if(policy == BPOL_mirror)
  {
    prepareNodeForWipingRecursively(metadata, node);
  }
  else
  {
    markAsRemovedRecursively(metadata, node, true);
  }
}

/** Checks if the content of a regular file has changed.

  @param node A node representing a file with a size greater than 0 at its
  current history point. Its size should not have changed since the last
  backup. This function will update the node if the file has changed.
  @param result The search result which has found the given node.
*/
static void checkFileContentChanges(PathNode *node, PathState *state,
                                    SearchResult result)
{
  uint8_t hash[FILE_HASH_SIZE];
  size_t bytes_used = FILE_HASH_SIZE;

  if(state->metadata.reg.size > FILE_HASH_SIZE)
  {
    fileHash(node->path.str, result.stats, hash);
  }
  else
  {
    bytes_used = state->metadata.reg.size;

    FileStream *stream = sFopenRead(node->path.str);
    sFread(hash, bytes_used, stream);
    bool stream_not_at_end = sFbytesLeft(stream);
    sFclose(stream);

    if(stream_not_at_end)
    {
      die("file has changed while checking for changes: \"%s\"",
          node->path.str);
    }
  }

  if(memcmp(state->metadata.reg.hash, hash, bytes_used) != 0)
  {
    node->hint |= BH_content_changed | BH_fresh_hash;
    memcpy(state->metadata.reg.hash, hash, bytes_used);
  }
}

/** Checks what has changed in the path described by the given node.

  @param node The node representing the path to check.
  @param state The path state which will be updated with the changes.
  @param result The search result which found the given node.
*/
static void handleNodeChanges(PathNode *node, PathState *state,
                              SearchResult result)
{
  if(state->uid != result.stats.st_uid ||
     state->gid != result.stats.st_gid)
  {
    node->hint |= BH_owner_changed;
    state->uid = result.stats.st_uid;
    state->gid = result.stats.st_gid;
  }

  /* Path state specific change checks. */
  if(state->type == PST_regular)
  {
    if(state->metadata.reg.mode != result.stats.st_mode)
    {
      node->hint |= BH_permissions_changed;
      state->metadata.reg.mode = result.stats.st_mode;
    }

    if(state->metadata.reg.timestamp != result.stats.st_mtime)
    {
      node->hint |= BH_timestamp_changed;
      state->metadata.reg.timestamp = result.stats.st_mtime;
    }

    if(state->metadata.reg.size != (uint64_t)result.stats.st_size)
    {
      node->hint |= BH_content_changed;
      state->metadata.reg.size = result.stats.st_size;
    }
    else if(node->hint & BH_timestamp_changed &&
            state->metadata.reg.size > 0)
    {
      checkFileContentChanges(node, state, result);
    }
  }
  else if(state->type == PST_symlink)
  {
    const char *sym_target = readSymlink(result);

    if(strcmp(state->metadata.sym_target, sym_target) != 0)
    {
      state->metadata.sym_target = sym_target;
      node->hint |= BH_content_changed;
    }
  }
  else if(state->type == PST_directory)
  {
    if(state->metadata.dir.mode != result.stats.st_mode)
    {
      node->hint |= BH_permissions_changed;
      state->metadata.dir.mode = result.stats.st_mode;
    }

    if(state->metadata.dir.timestamp != result.stats.st_mtime)
    {
      node->hint |= BH_timestamp_changed;
      state->metadata.dir.timestamp = result.stats.st_mtime;
    }
  }
}

/** Checks changes in a node which already existed at the previous backup.

  @param metadata The metadata of the current backup.
  @param node The node to check for changes.
  @param result The search result which has matched the given node.
*/
static void handleFoundNode(Metadata *metadata, PathNode *node,
                            SearchResult result)
{
  if(result.policy != BPOL_track)
  {
    handleNodeChanges(node, &node->history->state, result);

    if(node->hint != BH_none)
    {
      reassignPointToCurrent(metadata, node->history);
    }
    else
    {
      node->hint = BH_unchanged;
      if(result.policy == BPOL_none)
      {
        reassignPointToCurrent(metadata, node->history);
      }
    }
  }
  else if(node->history->state.type == PST_non_existing)
  {
    node->hint = BH_added;

    PathHistory *point = buildPathHistoryPoint(metadata, result);

    point->next = node->history;
    node->history = point;
  }
  else
  {
    PathHistory *point = mpAlloc(sizeof *point);
    point->state = node->history->state;

    handleNodeChanges(node, &point->state, result);

    if(node->hint == BH_none)
    {
      node->hint = BH_unchanged;
    }
    else
    {
      point->backup = &metadata->current_backup;
      point->backup->ref_count = sSizeAdd(point->backup->ref_count, 1);

      point->next = node->history;
      node->history = point;
    }
  }
}

/** Checks which nodes where not found during the backup and handles them.

  @param metadata The metadata of the backup.
  @param node_match The SearchNode which has matched/found the current
  PathNode. Can be NULL.
  @param node_policy The policy under which the current node was found.
  @param subnode_list The list of the current nodes subnodes. Can be NULL.
  @param ignore_list The ignore expression list of the current backups
  search tree. Can be NULL.
*/
static void handleNotFoundSubnodes(Metadata *metadata,
                                   SearchNode *node_match,
                                   BackupPolicy node_policy,
                                   PathNode *subnode_list,
                                   RegexList *ignore_list)
{
  for(PathNode *subnode = subnode_list;
      subnode != NULL; subnode = subnode->next)
  {
    if(subnode->hint != BH_none)
    {
      continue;
    }

    /* Find the node in the search tree matching the current subnode. */
    SearchNode *subnode_match =
      matchesSearchSubnodes(subnode->path, node_match);
    if(subnode_match != NULL)
    {
      handleRemovedPath(metadata, subnode, subnode_match->policy);
    }
    else if(node_policy == BPOL_none ||
            matchesIgnoreList(subnode->path, ignore_list))
    {
      prepareNodeForWipingRecursively(metadata, subnode);
    }
    else
    {
      handleRemovedPath(metadata, subnode, node_policy);
    }
  }
}

/** Queries and processes the next search result recursively and updates
  the given metadata as described in the documentation of initiateBackup().

  @param metadata The metadata which should be updated.
  @param node_list A pointer to the node list corresponding to the
  currently traversed directory.
  @param context The context from which the search result should be
  queried.
  @param ignore_list The ignore list of the search tree used to build the
  given search context. Can be NULL.

  @return The type of the processed result.
*/
static SearchResultType initiateMetadataRecursively(Metadata *metadata,
                                                    PathNode **node_list,
                                                    SearchContext *context,
                                                    RegexList *ignore_list)
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

    node->hint = BH_added;
    node->policy = result.policy;
    node->history = buildPathHistoryPoint(metadata, result);
    node->subnodes = NULL;

    /* Prepend the new node to the current node list. */
    node->next = *node_list;
    *node_list = node;

    metadata->total_path_count = sSizeAdd(metadata->total_path_count, 1);
  }
  else
  {
    handleFoundNode(metadata, node, result);
  }

  if(result.type == SRT_directory)
  {
    while(initiateMetadataRecursively(metadata, &node->subnodes,
                                      context, ignore_list)
          != SRT_end_of_directory);
  }

  handleNotFoundSubnodes(metadata, result.node, result.policy,
                         node->subnodes, ignore_list);

  /* Mark nodes without a policy and needed subnodes for purging. */
  if(result.policy == BPOL_none)
  {
    bool has_needed_subnode = false;
    for(PathNode *subnode = node->subnodes;
        subnode != NULL; subnode = subnode->next)
    {
      if(subnode->hint != BH_not_part_of_repository)
      {
        has_needed_subnode = true;
        break;
      }
    }

    if(has_needed_subnode == false)
    {
      prepareNodeForWiping(metadata, node);
    }
  }

  return result.type;
}

/** Copies the file represented by the given node into the repository.

  @param node A PathNode which represents a regular file at its current
  history point.
  @param repo_path The path to the backup repository.
  @param repo_tmp_file_path The path to the repositories temporary file.
  @param stats The stats of the file represented by the node. Required to
  determine the ideal block size.
*/
static void copyFileIntoRepo(PathNode *node, const char *repo_path,
                             const char *repo_tmp_file_path,
                             struct stat stats)
{
  RegularFileInfo *reg = &node->history->state.metadata.reg;
  size_t blocksize = stats.st_blksize;
  uint64_t bytes_left = reg->size;

  FileStream *reader = sFopenRead(node->path.str);
  RepoWriter *writer = repoWriterOpenFile(repo_path, repo_tmp_file_path,
                                          node->path.str, reg);
  char *buffer = sMalloc(blocksize);

  while(bytes_left > 0)
  {
    size_t bytes_to_read = bytes_left > blocksize ? blocksize : bytes_left;

    sFread(buffer, bytes_to_read, reader);
    repoWriterWrite(buffer, bytes_to_read, writer);

    bytes_left -= bytes_to_read;
  }

  free(buffer);

  bool stream_not_at_end = sFbytesLeft(reader);
  sFclose(reader);

  if(stream_not_at_end)
  {
    die("file has changed during backup: \"%s\"", node->path.str);
  }

  repoWriterClose(writer);
}

/** Checks if the file represented by the given node is equal to its stored
  counterpart in the backup repository.

  @param node A PathNode which represents a regular file at its current
  history point. Its hash and slot number must be set to the stored file it
  should be compared to.
  @param repo_path The path to the backup repository.
  @param stats The stats of the file represented by the node. Required to
  determine the ideal block size.

  @return True if the file represented by the node is equal to its stored
  counterpart.
*/
static bool equalsToStoredFile(PathNode *node, const char *repo_path,
                               struct stat stats)
{
  RegularFileInfo *reg = &node->history->state.metadata.reg;
  size_t blocksize = stats.st_blksize;
  uint64_t bytes_left = reg->size;

  FileStream *stream = sFopenRead(node->path.str);
  char *buffer = sMalloc(sSizeMul(blocksize, 2));

  RepoReader *repo_stream =
    repoReaderOpenFile(repo_path, node->path.str, reg);
  char *repo_buffer = &buffer[blocksize];

  bool files_equal = true;

  while(bytes_left > 0 && files_equal)
  {
    size_t bytes_to_read = bytes_left > blocksize ? blocksize : bytes_left;

    sFread(buffer, bytes_to_read, stream);
    repoReaderRead(repo_buffer, bytes_to_read, repo_stream);

    files_equal = memcmp(buffer, repo_buffer, bytes_to_read) == 0;

    bytes_left -= bytes_to_read;
  }

  repoReaderClose(repo_stream);
  free(buffer);

  bool stream_not_at_end = sFbytesLeft(stream);
  sFclose(stream);

  if(stream_not_at_end)
  {
    die("file has changed while comparing to backup: \"%s\"",
        node->path.str);
  }

  return files_equal;
}

/** Checks if the regular file represented by the given node already exists
  in the repository.

  @param node A PathNode which represents a regular file at its current
  history point. Its hash must be set and its slot number will be modified
  by this function.
  @param repo_path The path to the backup repository.
  @param stats The stats of the file represented by the node. Required to
  determine the ideal block size.

  @return True if the file already exists. In this case the nodes slot
  number will be set to the already existing files slot number. If false is
  returned, the nodes slot number will contain the next free slot number.
*/
static bool searchFileDuplicates(PathNode *node, const char *repo_path,
                                 struct stat stats)
{
  RegularFileInfo *reg = &node->history->state.metadata.reg;
  String repo_path_str = str(repo_path);
  reg->slot = 0;

  while(repoRegularFileExists(repo_path_str, reg))
  {
    if(equalsToStoredFile(node, repo_path, stats))
    {
      return true;
    }
    else if(reg->slot == UINT8_MAX)
    {
      die("overflow calculating slot number");
    }

    reg->slot++;
  }

  return false;
}

/** Adds/copies a file to the repository.

  @param node A PathNode which represents a regular file at its current
  history point. Its hash and slot number will be set by this function. In
  some cases the entire file will be stored as the hash. See the
  documentation of RegularFileInfo for more informations.
  @param repo_path The path to the repository.
  @param repo_tmp_file_path The path to the repositories temporary file.
*/
static void addFileToRepo(PathNode *node, const char *repo_path,
                          const char *repo_tmp_file_path)
{
  RegularFileInfo *reg = &node->history->state.metadata.reg;

  /* Die if the file has changed since the metadata was initiated. */
  struct stat stats = sStat(node->path.str);
  if(node->history->state.metadata.reg.timestamp != stats.st_mtime)
  {
    die("file has changed during backup: \"%s\"", node->path.str);
  }
  else if(reg->size > FILE_HASH_SIZE)
  {
    if((node->hint & BH_fresh_hash) == false)
    {
      fileHash(node->path.str, stats, reg->hash);
    }

    if(searchFileDuplicates(node, repo_path, stats) == false)
    {
      copyFileIntoRepo(node, repo_path, repo_tmp_file_path, stats);
    }
  }
  else if((node->hint & BH_fresh_hash) == false)
  {
    /* Store small files directly in its hash buffer. */
    FileStream *stream = sFopenRead(node->path.str);
    sFread(&reg->hash, reg->size, stream);
    bool stream_not_at_end = sFbytesLeft(stream);
    sFclose(stream);

    if(stream_not_at_end)
    {
      die("file has changed during backup: \"%s\"", node->path.str);
    }
  }
}

/** Finishes a backup recursively, as described in the documentation of
  finishBackup().

  @param metadata A valid metadata struct as described in the documentation
  of finishBackup().
  @param node_list A list containing the subnodes of the currently
  traversed node.
  @param repo_path The path to the repository.
  @param repo_tmp_file_path The path to the repositories temporary file.
*/
static void finishBackupRecursively(Metadata *metadata,
                                    PathNode *node_list,
                                    const char *repo_path,
                                    const char *repo_tmp_file_path)
{
  for(PathNode *node = node_list; node != NULL; node = node->next)
  {
    /* Handle only new regular files. */
    if(node->history->state.type == PST_regular &&
       node->history->state.metadata.reg.size > 0 &&
       (node->hint == BH_added || (node->hint & BH_content_changed)))
    {
      addFileToRepo(node, repo_path, repo_tmp_file_path);
    }

    finishBackupRecursively(metadata, node->subnodes,
                            repo_path, repo_tmp_file_path);
  }
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
  while(initiateMetadataRecursively(metadata, &metadata->paths, context,
                                    *root_node->ignore_expressions)
        != SRT_end_of_search);

  handleNotFoundSubnodes(metadata, root_node, root_node->policy,
                         metadata->paths, *root_node->ignore_expressions);
}

/** Completes a backup initiated with initiateBackup(). It copies
  new/changed files to the repository and calculates missing hashes and
  slot numbers.

  @param metadata A valid metadata struct which was successfully initiated
  using initiateBackup(). This struct will be finalized and should never be
  passed to this function again.
  @param repo_path The path to the repository.
  @param repo_tmp_file_path The path to the repositories temporary file.
*/
void finishBackup(Metadata *metadata, const char *repo_path,
                  const char *repo_tmp_file_path)
{
  finishBackupRecursively(metadata, metadata->paths, repo_path,
                          repo_tmp_file_path);
  metadata->current_backup.timestamp = sTime();
}
