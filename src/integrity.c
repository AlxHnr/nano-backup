/** @file
  Implements integrity checks for repositories.
*/

#include "integrity.h"

#include <string.h>

#include "CRegion/alloc-growable.h"
#include "file-hash.h"
#include "path-builder.h"
#include "safe-wrappers.h"

/** Check the integrity of a single stored file.

  @param file_info Metadata of the file to check.
  @param path_to_stored_file Absolute or relative path to the unique file
  inside the backup repository.

  @return True if the given file is healthy.
*/
static bool storedFileIsHealthy(RegularFileInfo *file_info,
                                String path_to_stored_file)
{
  if(file_info->size <= FILE_HASH_SIZE)
  {
    return true;
  }
  if(!sPathExists(path_to_stored_file))
  {
    return false;
  }

  const struct stat stats = sLStat(path_to_stored_file);
  if(!S_ISREG(stats.st_mode))
  {
    return false;
  }
  if(stats.st_size != file_info->size)
  {
    return false;
  }

  uint8_t hash[FILE_HASH_SIZE];
  fileHash(path_to_stored_file, stats, hash);
  return memcmp(file_info->hash, hash, FILE_HASH_SIZE) == 0;
}

/** Validate the integrity of all files in the given subtree recursively.

  @param r Used for allocating additional broken nodes.
  @param node_list List of path nodes to traverse recursively.
  @param broken_nodes Will be updated with newly found broken nodes by
  prepending to this list.
  @param path_buffer Reusable buffer pre-populated with the full or
  relative path to the repository.
  @param unique_subpath_buffer Reusable string buffer for internal use by
  this function.
  @param repo_path_length Length of the pre-populated path in
  `path_buffer`, excluding the trailing slash.
*/
static void checkIntegrityRecursively(CR_Region *r,
                                      PathNode *node_list,
                                      ListOfBrokenPathNodes **broken_nodes,
                                      char **path_buffer,
                                      char **unique_subpath_buffer,
                                      const size_t repo_path_length)
{
  for(PathNode *node = node_list; node != NULL; node = node->next)
  {
    for(PathHistory *point = node->history;
        point != NULL; point = point->next)
    {
      if(point->state.type != PST_regular)
      {
        continue;
      }

      repoBuildRegularFilePath(unique_subpath_buffer,
                               &point->state.metadata.reg);
      const size_t file_buffer_length =
        pathBuilderAppend(path_buffer, repo_path_length,
                          *unique_subpath_buffer);
      String unique_path = strSlice(*path_buffer, file_buffer_length);

      if(!storedFileIsHealthy(&point->state.metadata.reg, unique_path))
      {
        ListOfBrokenPathNodes *broken_node =
          CR_RegionAlloc(r, sizeof(*broken_node));
        broken_node->node = node;
        broken_node->next = *broken_nodes;
        *broken_nodes = broken_node;
        break;
      }
    }
    checkIntegrityRecursively(r, node->subnodes, broken_nodes, path_buffer,
                              unique_subpath_buffer, repo_path_length);
  }
}


/** Check if all the files in the specified repository match up with their
  stored hash.

  @param r Region used for allocating the returned result.
  @param metadata Repository to validate.
  @param repo_path Absolute or relative path to the repository to check.

  @return NULL if the given repository is healthy. Otherwise a list of all
  nodes associated with corrupted files. The lifetime of the returned list
  will be bound to the given region.
*/
ListOfBrokenPathNodes *checkIntegrity(CR_Region *r, Metadata *metadata,
                                      String repo_path)
{
  ListOfBrokenPathNodes *result = NULL;

  CR_Region *disposable_r = CR_RegionNew();
  char *unique_subpath_buffer = CR_RegionAllocGrowable(disposable_r, 1);

  char *path_buffer = CR_RegionAllocGrowable(disposable_r, 1);
  pathBuilderSet(&path_buffer, cStr(repo_path, &unique_subpath_buffer));

  checkIntegrityRecursively(r, metadata->paths, &result, &path_buffer,
                            &unique_subpath_buffer, repo_path.length);

  CR_RegionRelease(disposable_r);
  return result;
}
