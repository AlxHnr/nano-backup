/** @file
  Implements integrity checks for repositories.
*/

#include "integrity.h"

#include <string.h>

#include "CRegion/alloc-growable.h"
#include "file-hash.h"
#include "path-builder.h"
#include "safe-wrappers.h"

/** Stores the data of an ongoing integrity check. */
typedef struct
{
  /** Used for allocating broken path nodes. */
  CR_Region *r;

  /** All broken path nodes found during the check will be stored here. The
    lifetime of this list is bound to the region `r` in this struct. */
  ListOfBrokenPathNodes *broken_nodes;

  /** Absolute or relative path to the repository that is being checked. */
  String repo_path;

  /** Reusable buffer pre-populated with the content of repo_path. */
  char *path_buffer;

  /** Reusable buffer created with CR_RegionAllocGrowable(). */
  char *unique_subpath_buffer;
}IntegrityCheckContext;

/** Check the integrity of a single stored file.

  @param file_info Metadata of the file to check. The files size must be
  larger than FILE_HASH_SIZE.
  @param path_to_stored_file Absolute or relative path to the unique file
  inside the backup repository.

  @return True if the given file is healthy.
*/
static bool storedFileIsHealthy(RegularFileInfo *file_info,
                                String path_to_stored_file)
{
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

/** Check the integrity of the stored file associated with the given
  history point.

  @param context Informations related to the current integrity check.
  @param point History point to check.

  @return True if the given history point is healthy.
*/
static bool historyPointIsHealthy(IntegrityCheckContext *context,
                                  PathHistory *point)
{
  if(point->state.type != PST_regular)
  {
    return true;
  }

  RegularFileInfo *file_info = &point->state.metadata.reg;
  if(file_info->size <= FILE_HASH_SIZE)
  {
    return true;
  }

  repoBuildRegularFilePath(&context->unique_subpath_buffer, file_info);
  const size_t file_buffer_length =
    pathBuilderAppend(&context->path_buffer, context->repo_path.length,
                      context->unique_subpath_buffer);
  String unique_path = strSlice(context->path_buffer, file_buffer_length);

  return storedFileIsHealthy(file_info, unique_path);
}

/** Validate the integrity of all files in the given subtree recursively.

  @param context Pre-populated context to be used for this integrity check.
  @param node_list List of path nodes to traverse recursively.
*/
static void checkIntegrityRecursively(IntegrityCheckContext *context,
                                      PathNode *node_list)
{
  for(PathNode *node = node_list; node != NULL; node = node->next)
  {
    for(PathHistory *point = node->history;
        point != NULL; point = point->next)
    {
      if(!historyPointIsHealthy(context, point))
      {
        ListOfBrokenPathNodes *broken_node =
          CR_RegionAlloc(context->r, sizeof(*broken_node));
        broken_node->node = node;
        broken_node->next = context->broken_nodes;
        context->broken_nodes = broken_node;
        break;
      }
    }
    checkIntegrityRecursively(context, node->subnodes);
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
  CR_Region *disposable_r = CR_RegionNew();

  IntegrityCheckContext context = {
    .r = r,
    .broken_nodes = NULL,
    .repo_path = repo_path,
    .path_buffer = CR_RegionAllocGrowable(disposable_r, 1),
    .unique_subpath_buffer = CR_RegionAllocGrowable(disposable_r, 1),
  };
  pathBuilderSet(&context.path_buffer,
                 cStr(repo_path, &context.unique_subpath_buffer));

  checkIntegrityRecursively(&context, metadata->paths);

  CR_RegionRelease(disposable_r);
  return context.broken_nodes;
}
