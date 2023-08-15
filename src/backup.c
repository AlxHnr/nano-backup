#include "backup.h"

#include <stdlib.h>
#include <string.h>

#include "CRegion/alloc-growable.h"

#include "backup-helpers.h"
#include "error-handling.h"
#include "file-hash.h"
#include "memory-pool.h"
#include "repository.h"
#include "safe-math.h"
#include "safe-wrappers.h"
#include "search.h"

static unsigned char *io_buffer = NULL;

/** Sets all values inside the given state to the properties in the
  specified result. A regular files hash and slot will be left undefined.

  @param state The state to update.
  @param result The search result describing a found path, which must have
  the type SRT_regular, SRT_symlink or SRT_directory. Otherwise it will
  result in undefined behaviour.
*/
static void setPathHistoryState(PathState *state,
                                const SearchResult result)
{
  state->uid = result.stats.st_uid;
  state->gid = result.stats.st_gid;

  if(result.type == SRT_regular_file)
  {
    state->type = PST_regular_file;
    state->metadata.file_info.permission_bits = result.stats.st_mode;
    state->metadata.file_info.modification_time = result.stats.st_mtime;
    state->metadata.file_info.size = result.stats.st_size;
  }
  else if(result.type == SRT_symlink)
  {
    state->type = PST_symlink;
    static char *buffer = NULL;
    readSymlink(result.path, result.stats, &buffer);
    strSet(&state->metadata.symlink_target, strLegacyCopy(str(buffer)));
  }
  else if(result.type == SRT_directory)
  {
    state->type = PST_directory;
    state->metadata.directory_info.permission_bits = result.stats.st_mode;
    state->metadata.directory_info.modification_time =
      result.stats.st_mtime;
  }
}

/** Constructs a path history point from the given data.

  @param metadata The metadata to which the point belongs to.
  @param result The search result describing a found path, which must have
  the type SRT_regular, SRT_symlink or SRT_directory. Otherwise it will
  result in undefined behaviour.

  @return A new PathHistory point that should not be freed by the caller.
*/
static PathHistory *buildPathHistoryPoint(Metadata *metadata,
                                          const SearchResult result)
{
  PathHistory *point = mpAlloc(sizeof *point);

  point->backup = &metadata->current_backup;
  point->backup->ref_count = sSizeAdd(point->backup->ref_count, 1);

  setPathHistoryState(&point->state, result);

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
static bool searchNodeMatches(SearchNode *node, StringView path_tail)
{
  if(node->regex)
  {
    return regexec(node->regex, path_tail.content, 0, NULL, 0) == 0;
  }

  return strEqual(node->name, path_tail);
}

/** Checks if a subnode of the given result node matches the specified
  path.

  @param path The path to match. Must be null-terminated.
  @param result The node containing the subnodes used for matching. Can be
  NULL.

  @return The results subnode that has matched the given string, or NULL.
*/
static SearchNode *matchesSearchSubnodes(StringView path,
                                         const SearchNode *result)
{
  if(result != NULL)
  {
    StringView path_tail = strSplitPath(path).tail;
    for(SearchNode *node = result->subnodes; node != NULL;
        node = node->next)
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
static bool matchesIgnoreList(StringView path,
                              const RegexList *ignore_list)
{
  for(const RegexList *item = ignore_list; item != NULL; item = item->next)
  {
    if(regexec(item->regex, path.content, 0, NULL, 0) == 0)
    {
      return true;
    }
  }

  return false;
}

/** Decrements all reference counts in the given history list.

  @param first_point The first history point in the list. Can be NULL.
*/
static void decrementRefCounts(PathHistory *first_point)
{
  for(PathHistory *point = first_point; point != NULL; point = point->next)
  {
    point->backup->ref_count--;
  }
}

/** Marks the given node as BH_not_part_of_repository and decrements all
  reference counts it causes.

  @param metadata The metadata of the current backup.
  @param node The node to process.
*/
static void prepareNodeForWiping(Metadata *metadata, PathNode *node)
{
  if(backupHintNoPol(node->hint) == BH_not_part_of_repository)
  {
    return;
  }

  backupHintSet(node->hint, BH_not_part_of_repository);
  metadata->total_path_count--;

  decrementRefCounts(node->history);
}

static void prepareNodeForWipingRecursively(Metadata *metadata,
                                            PathNode *node)
{
  prepareNodeForWiping(metadata, node);
  for(PathNode *subnode = node->subnodes; subnode != NULL;
      subnode = subnode->next)
  {
    prepareNodeForWipingRecursively(metadata, subnode);
  }
}

/** Marks the given tree recursively as BH_removed. Tracked nodes which
  where removed at the previous backup will be marked as BH_unchanged.

  @param metadata The metadata of the current backup.
  @param node The node which should be updated recursively.
  @param extend_tracked_histories True if a tracked nodes history should be
  extended with a "removed" state.
*/
static void markAsRemovedRecursively(Metadata *metadata, PathNode *node,
                                     bool extend_tracked_histories)
{
  if(backupHintNoPol(node->hint) == BH_not_part_of_repository)
  {
    return;
  }
  if(node->history->state.type == PST_non_existing)
  {
    backupHintSet(node->hint, BH_unchanged);
  }
  else
  {
    backupHintSet(node->hint, BH_removed);
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

  for(PathNode *subnode = node->subnodes; subnode != NULL;
      subnode = subnode->next)
  {
    markAsRemovedRecursively(metadata, subnode, extend_tracked_histories);
  }
}

/** Checks and handles policy changes.

  @param metadata The metadata of the current backup.
  @param node The node to check for changes.
  @param policy The policy under which the node was found, or supposed to
  be found.
*/
static void handlePolicyChanges(Metadata *metadata, PathNode *node,
                                const BackupPolicy policy)
{
  if(node->policy == policy)
  {
    return;
  }

  backupHintSet(node->hint, BH_policy_changed);
  if(node->policy == BPOL_track)
  {
    if(node->history->state.type == PST_non_existing)
    {
      node->history->backup->ref_count--;
      node->history = node->history->next;
    }

    if(node->history->next != NULL)
    {
      decrementRefCounts(node->history->next);
      node->history->next = NULL;

      backupHintSet(node->hint, BH_loses_history);
    }

    if(node->history->state.type != PST_directory)
    {
      for(PathNode *subnode = node->subnodes; subnode != NULL;
          subnode = subnode->next)
      {
        prepareNodeForWipingRecursively(metadata, subnode);
      }
    }
  }

  node->policy = policy;
}

/** Handles a node, which path was removed from the users filesystem.

  @param metadata The metadata of the current backup.
  @param node The node representing the removed file.
  @param policy The policy which the removed path is supposed to have.
*/
static void handleRemovedPath(Metadata *metadata, PathNode *node,
                              const BackupPolicy policy)
{
  handlePolicyChanges(metadata, node, policy);

  if(policy == BPOL_mirror)
  {
    prepareNodeForWipingRecursively(metadata, node);
  }
  else if(policy == BPOL_none &&
          (node->subnodes == NULL ||
           node->history->state.type != PST_directory))
  {
    prepareNodeForWiping(metadata, node);
  }
  else
  {
    markAsRemovedRecursively(metadata, node, true);
  }
}

/** Checks if the filetype of the given node has changed and updates its
  backup hint accordingly.

  @param node The node describing the path to check.
  @param result The search result which has matched the path.
*/
static void handleFiletypeChanges(PathNode *node,
                                  const SearchResult result)
{
  if(node->history->state.type == PST_regular_file)
  {
    if(result.type == SRT_symlink)
    {
      backupHintSet(node->hint, BH_regular_to_symlink);
    }
    else if(result.type == SRT_directory)
    {
      backupHintSet(node->hint, BH_regular_to_directory);
    }
  }
  else if(node->history->state.type == PST_symlink)
  {
    if(result.type == SRT_regular_file)
    {
      backupHintSet(node->hint, BH_symlink_to_regular);
    }
    else if(result.type == SRT_directory)
    {
      backupHintSet(node->hint, BH_symlink_to_directory);
    }
  }
  else if(node->history->state.type == PST_directory)
  {
    if(result.type == SRT_regular_file)
    {
      backupHintSet(node->hint, BH_directory_to_regular);
    }
    else if(result.type == SRT_symlink)
    {
      backupHintSet(node->hint, BH_directory_to_symlink);
    }
  }
}

/** Checks what has changed in the path described by the given node.

  @param node The node containing the backup hint to update.
  @param state The path state which will be updated with the changes.
  @param result The search result which matched the given node.
*/
static void handleNodeChanges(PathNode *node, PathState *state,
                              const SearchResult result)
{
  handleFiletypeChanges(node, result);

  if(backupHintNoPol(node->hint) == BH_none)
  {
    applyNodeChanges(node, state, result.stats);
  }
  else if(result.policy != BPOL_none)
  {
    setPathHistoryState(state, result);
  }
}

/** Checks changes in a node which already existed at the previous backup.

  @param metadata The metadata of the current backup.
  @param node The node to check for changes.
  @param result The search result which has matched the given node.
*/
static void handleFoundNode(Metadata *metadata, PathNode *node,
                            const SearchResult result)
{
  handlePolicyChanges(metadata, node, result.policy);

  if(result.policy != BPOL_track)
  {
    handleNodeChanges(node, &node->history->state, result);

    if(backupHintNoPol(node->hint) != BH_none ||
       result.policy == BPOL_none)
    {
      reassignPointToCurrent(metadata, node->history);
    }
  }
  else if(node->history->state.type == PST_non_existing)
  {
    backupHintSet(node->hint, BH_added);

    PathHistory *point = buildPathHistoryPoint(metadata, result);

    point->next = node->history;
    node->history = point;
  }
  else
  {
    PathState state = node->history->state;
    handleNodeChanges(node, &state, result);

    if(backupHintNoPol(node->hint) != BH_none)
    {
      PathHistory *point = mpAlloc(sizeof *point);

      memcpy(&point->state, &state, sizeof(point->state));
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
                                   const SearchNode *node_match,
                                   const BackupPolicy node_policy,
                                   PathNode *subnode_list,
                                   const RegexList *ignore_list)
{
  for(PathNode *subnode = subnode_list; subnode != NULL;
      subnode = subnode->next)
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
static SearchResultType
initiateMetadataRecursively(Metadata *metadata, PathNode **node_list,
                            SearchIterator *context,
                            const RegexList *ignore_list)
{
  const SearchResult result = searchGetNext(context);
  if(result.type == SRT_end_of_directory ||
     result.type == SRT_end_of_search || result.type == SRT_other)
  {
    return result.type;
  }

  PathNode *node = strTableGet(metadata->path_table, result.path);

  if(node == NULL)
  {
    node = mpAlloc(sizeof *node);

    StringView path_copy = strLegacyCopy(result.path);
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
    while(initiateMetadataRecursively(metadata, &node->subnodes, context,
                                      ignore_list) != SRT_end_of_directory)
      ;
  }

  if(backupHintNoPol(node->hint) == BH_directory_to_regular ||
     backupHintNoPol(node->hint) == BH_directory_to_symlink)
  {
    if(result.policy == BPOL_none || result.policy == BPOL_track)
    {
      for(PathNode *subnode = node->subnodes; subnode != NULL;
          subnode = subnode->next)
      {
        markAsRemovedRecursively(metadata, subnode,
                                 result.policy == BPOL_track);
      }
    }
    else
    {
      for(PathNode *subnode = node->subnodes; subnode != NULL;
          subnode = subnode->next)
      {
        prepareNodeForWipingRecursively(metadata, subnode);
      }
    }
  }
  else if(result.policy == BPOL_track &&
          node->history->state.type == PST_regular_file)
  {
    for(PathNode *subnode = node->subnodes; subnode != NULL;
        subnode = subnode->next)
    {
      markAsRemovedRecursively(metadata, subnode, false);
    }
  }
  else
  {
    handleNotFoundSubnodes(metadata, result.node, result.policy,
                           node->subnodes, ignore_list);
  }

  /* Mark nodes without a policy and needed subnodes for purging. */
  if(result.policy == BPOL_none)
  {
    bool has_needed_subnode = false;
    for(PathNode *subnode = node->subnodes; subnode != NULL;
        subnode = subnode->next)
    {
      if(backupHintNoPol(subnode->hint) != BH_not_part_of_repository)
      {
        has_needed_subnode = true;
        break;
      }
    }

    if(!has_needed_subnode)
    {
      prepareNodeForWiping(metadata, node);
    }
  }

  if(node->hint == BH_none)
  {
    backupHintSet(node->hint, BH_unchanged);
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
static void copyFileIntoRepo(PathNode *node, StringView repo_path,
                             StringView repo_tmp_file_path,
                             const struct stat stats)
{
  const RegularFileInfo *file_info =
    &node->history->state.metadata.file_info;
  const size_t blocksize = stats.st_blksize;
  uint64_t bytes_left = file_info->size;

  FileStream *reader = sFopenRead(node->path);
  RepoWriter *writer = repoWriterOpenFile(repo_path, repo_tmp_file_path,
                                          node->path, file_info);

  io_buffer = CR_EnsureCapacity(io_buffer, blocksize);

  while(bytes_left > 0)
  {
    const size_t bytes_to_read =
      bytes_left > blocksize ? blocksize : bytes_left;

    sFread(io_buffer, bytes_to_read, reader);
    repoWriterWrite(io_buffer, bytes_to_read, writer);

    bytes_left -= bytes_to_read;
  }

  const bool stream_not_at_end = sFbytesLeft(reader);
  sFclose(reader);

  if(stream_not_at_end)
  {
    die("file has changed during backup: \"%s\"", node->path.content);
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
static bool equalsToStoredFile(const PathNode *node, StringView repo_path,
                               const struct stat stats)
{
  const RegularFileInfo *file_info =
    &node->history->state.metadata.file_info;
  const size_t blocksize = stats.st_blksize;

  FileStream *stream = sFopenRead(node->path);

  io_buffer = CR_EnsureCapacity(io_buffer, sSizeMul(blocksize, 2));

  RepoReader *repo_stream =
    repoReaderOpenFile(repo_path, node->path, file_info);
  unsigned char *repo_buffer = &io_buffer[blocksize];

  uint64_t bytes_left = file_info->size;
  bool files_equal = true;
  while(bytes_left > 0 && files_equal)
  {
    const size_t bytes_to_read =
      bytes_left > blocksize ? blocksize : bytes_left;

    sFread(io_buffer, bytes_to_read, stream);
    repoReaderRead(repo_buffer, bytes_to_read, repo_stream);

    files_equal = memcmp(io_buffer, repo_buffer, bytes_to_read) == 0;

    bytes_left -= bytes_to_read;
  }

  repoReaderClose(repo_stream);

  const bool stream_not_at_end = sFbytesLeft(stream);
  sFclose(stream);

  if(bytes_left == 0 && stream_not_at_end)
  {
    die("file has changed while comparing to backup: \"%s\"",
        node->path.content);
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
static bool searchFileDuplicates(PathNode *node, StringView repo_path,
                                 const struct stat stats)
{
  RegularFileInfo *file_info = &node->history->state.metadata.file_info;
  file_info->slot = 0;

  while(repoRegularFileExists(repo_path, file_info))
  {
    if(equalsToStoredFile(node, repo_path, stats))
    {
      return true;
    }
    if(file_info->slot == UINT8_MAX)
    {
      die("overflow calculating slot number");
    }

    file_info->slot++;
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
static void addFileToRepo(PathNode *node, StringView repo_path,
                          StringView repo_tmp_file_path)
{
  RegularFileInfo *file_info = &node->history->state.metadata.file_info;

  /* Die if the file has changed since the metadata was initiated. */
  const struct stat stats = sStat(node->path);
  if(node->history->state.metadata.file_info.modification_time !=
     stats.st_mtime)
  {
    die("file has changed during backup: \"%s\"", node->path.content);
  }
  else if(file_info->size > FILE_HASH_SIZE)
  {
    if(!(node->hint & BH_fresh_hash))
    {
      fileHash(node->path, stats, file_info->hash);
    }

    if(!searchFileDuplicates(node, repo_path, stats))
    {
      copyFileIntoRepo(node, repo_path, repo_tmp_file_path, stats);
    }
  }
  else if(!(node->hint & BH_fresh_hash))
  {
    /* Store small files directly in its hash buffer. */
    FileStream *stream = sFopenRead(node->path);
    sFread(&file_info->hash, file_info->size, stream);
    const bool stream_not_at_end = sFbytesLeft(stream);
    sFclose(stream);

    if(stream_not_at_end)
    {
      die("file has changed during backup: \"%s\"", node->path.content);
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
                                    StringView repo_path,
                                    StringView repo_tmp_file_path)
{
  for(PathNode *node = node_list; node != NULL; node = node->next)
  {
    /* Handle only new regular files. */
    if(node->history->state.type == PST_regular_file &&
       node->history->state.metadata.file_info.size > 0 &&
       (backupHintNoPol(node->hint) == BH_added ||
        backupHintNoPol(node->hint) == BH_symlink_to_regular ||
        backupHintNoPol(node->hint) == BH_directory_to_regular ||
        (node->hint & BH_content_changed)))
    {
      addFileToRepo(node, repo_path, repo_tmp_file_path);
    }

    finishBackupRecursively(metadata, node->subnodes, repo_path,
                            repo_tmp_file_path);
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
  SearchIterator *context = searchNew(root_node);
  while(initiateMetadataRecursively(metadata, &metadata->paths, context,
                                    *root_node->ignore_expressions) !=
        SRT_end_of_search)
    ;

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
void finishBackup(Metadata *metadata, StringView repo_path,
                  StringView repo_tmp_file_path)
{
  finishBackupRecursively(metadata, metadata->paths, repo_path,
                          repo_tmp_file_path);
  metadata->current_backup.completion_time = sTime();
}
