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

/** Checks if a subnode of the given result node matches the path in the
  specified PathNode.

  @param path The path to match. Must be null-terminated.
  @param result_node The result node containing the subnodes used for
  matching. Can be NULL.

  @return True or false.
*/
static bool matchesSearchSubnodes(String path, SearchNode *result_node)
{
  if(result_node != NULL)
  {
    String path_tail = strSplitPath(path).tail;
    for(SearchNode *node = result_node->subnodes;
        node != NULL; node = node->next)
    {
      if(searchNodeMatches(node, path_tail))
      {
        return true;
      }
    }
  }

  return false;
}

/** Matches the given ignore expression list against the specified path.

  @param path A null-terminated path which should be matched.
  @param ignore_list A list of ignore expressions or NULL.

  @return True, if one ignore expression matched the specified path.
*/
static bool matchesIgnoreList(String path, RegexList *ignore_list)
{
  for(RegexList *item = ignore_list;
      item != NULL; item = item->next)
  {
    if(regexec(item->regex, path.str, 0, NULL, 0) == 0)
    {
      return true;
    }
  }

  return false;
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
    node->hint = BH_unchanged;
    if(node->policy == BPOL_none)
    {
      reassignPointToCurrent(metadata, node->history);
    }
  }

  if(result.type == SRT_directory)
  {
    while(initiateMetadataRecursively(metadata, &node->subnodes,
                                      context, ignore_list)
          != SRT_end_of_directory);
  }

  for(PathNode *subnode = node->subnodes;
      subnode != NULL; subnode = subnode->next)
  {
    if(subnode->hint != BH_none)
    {
      continue;
    }
    else if(matchesSearchSubnodes(subnode->path, result.node))
    {
      subnode->hint = BH_removed;
    }
    else if(node->policy == BPOL_none ||
            matchesIgnoreList(node->path, ignore_list))
    {
      subnode->hint = BH_not_part_of_repository;
    }
    else
    {
      subnode->hint = BH_removed;
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
  uint64_t blocksize  = stats.st_blksize;
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
    die("file changed during backup: \"%s\"", node->path.str);
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
  uint64_t blocksize  = stats.st_blksize;
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
    die("file changed while comparing to backup: \"%s\"", node->path.str);
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

/** Adds/copies a new file to the repository.

  @param node A PathNode which represents a regular file at its current
  history point. Its hash and slot number will be set by this function. In
  some cases the entire file will be stored as the hash. See the
  documentation of RegularFileInfo for more informations.
  @param repo_path The path to the repository.
  @param repo_tmp_file_path The path to the repositories temporary file.
*/
static void addNewFileToRepo(PathNode *node, const char *repo_path,
                             const char *repo_tmp_file_path)
{
  RegularFileInfo *reg = &node->history->state.metadata.reg;

  /* Die if the file has changed since the metadata was initiated. */
  struct stat stats = sStat(node->path.str);
  if(node->history->state.timestamp != stats.st_mtime ||
     reg->size != (uint64_t)stats.st_size)
  {
    die("file has changed during backup: \"%s\"", node->path.str);
  }
  else if(reg->size <= FILE_HASH_SIZE)
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
  else
  {
    fileHash(node->path.str, stats, reg->hash);
    if(searchFileDuplicates(node, repo_path, stats) == false)
    {
      copyFileIntoRepo(node, repo_path, repo_tmp_file_path, stats);
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
    if(node->history->backup == &metadata->current_backup &&
       node->history->next == NULL &&
       node->history->state.type == PST_regular &&
       node->history->state.metadata.reg.size > 0)
    {
      addNewFileToRepo(node, repo_path, repo_tmp_file_path);
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
