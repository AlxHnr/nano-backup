#include "integrity.h"

#include <string.h>

#include "CRegion/alloc-growable.h"
#include "file-hash.h"
#include "safe-math.h"
#include "safe-wrappers.h"
#include "string-table.h"

typedef struct
{
  /** Used for allocating broken path nodes. */
  CR_Region *r;

  /** All broken path nodes found during the check will be stored here. The
    lifetime of this list is bound to the region `r` in this struct. */
  ListOfBrokenPathNodes *broken_nodes;

  StringView repo_path;

  /** Wraps a single reusable buffer for path building. */
  Allocator *reusable_buffer_allocator;

  /** Reusable buffer created with CR_RegionAllocGrowable(). */
  char *unique_subpath_buffer;

  /** Cache to store the result of checked files. */
  StringTable *unique_subpath_cache;
  Allocator *unique_subpath_cache_key_allocator;

  /** Volume of all files inside the repository with a size larger than
    FILE_HASH_SIZE. */
  uint64_t files_to_check_total_size;
} IntegrityCheckContext;

/**
  @param file_info Metadata of the file to check. The files size must be
  larger than FILE_HASH_SIZE.
  @param unique_subpath Path to the stored file relative to the repository.

  @return True if the specified file is healthy.
*/
typedef bool CheckFileCallback(IntegrityCheckContext *ctx,
                               const RegularFileInfo *file_info,
                               StringView unique_subpath);

/** Updates `ctx->files_to_hash_total_size`. */
static bool addToTotalFileSize(IntegrityCheckContext *ctx,
                               const RegularFileInfo *file_info,
                               StringView ignored)
{
  (void)ignored;

  ctx->files_to_check_total_size =
    sUint64Add(ctx->files_to_check_total_size, file_info->size);
  return true;
}

/**
  @param file_info Metadata of the file to check. The files size must be
  larger than FILE_HASH_SIZE.
  @param unique_subpath Path to the stored file relative to the
  repositories directory.
*/
static bool storedFileIsHealthy(IntegrityCheckContext *ctx,
                                const RegularFileInfo *file_info,
                                StringView unique_subpath)
{
  StringView path_to_stored_file = strAppendPath(
    ctx->repo_path, unique_subpath, ctx->reusable_buffer_allocator);

  if(!sPathExists(path_to_stored_file))
  {
    return false;
  }

  const struct stat stats = sLStat(path_to_stored_file);
  if(!S_ISREG(stats.st_mode))
  {
    return false;
  }
  if((uint64_t)stats.st_size != file_info->size)
  {
    return false;
  }

  uint8_t hash[FILE_HASH_SIZE];
  fileHash(path_to_stored_file, stats, hash, NULL, NULL);
  return memcmp(file_info->hash, hash, FILE_HASH_SIZE) == 0;
}

/** Check the integrity of the stored file associated with the given
  history point.

  @param ctx Informations related to the current integrity check.
  @param point History point to check.

  @return True if the given history point is healthy.
*/
static bool historyPointIsHealthy(IntegrityCheckContext *ctx,
                                  const PathHistory *point,
                                  CheckFileCallback check_file_callback)
{
  if(point->state.type != PST_regular_file)
  {
    return true;
  }

  const RegularFileInfo *file_info = &point->state.metadata.file_info;
  if(file_info->size <= FILE_HASH_SIZE)
  {
    return true;
  }

  repoBuildRegularFilePath(&ctx->unique_subpath_buffer, file_info);
  StringView unique_subpath = str(ctx->unique_subpath_buffer);

  const void *cached_result =
    strTableGet(ctx->unique_subpath_cache, unique_subpath);
  void *subpath_is_healthy = (void *)0x1;
  void *subpath_is_broken = (void *)0x2;

  if(cached_result == NULL)
  {
    const bool is_healthy =
      check_file_callback(ctx, file_info, unique_subpath);
    StringView key_copy =
      strCopy(unique_subpath, ctx->unique_subpath_cache_key_allocator);
    strTableMap(ctx->unique_subpath_cache, key_copy,
                is_healthy ? subpath_is_healthy : subpath_is_broken);
    return is_healthy;
  }
  return cached_result == subpath_is_healthy;
}

/** Validate the integrity of all files in the given subtree recursively.

  @param node_list List of path nodes to traverse recursively.
*/
static void
checkIntegrityRecursively(IntegrityCheckContext *ctx,
                          const PathNode *node_list,
                          CheckFileCallback check_file_callback)
{
  for(const PathNode *node = node_list; node != NULL; node = node->next)
  {
    for(const PathHistory *point = node->history; point != NULL;
        point = point->next)
    {
      if(!historyPointIsHealthy(ctx, point, check_file_callback))
      {
        ListOfBrokenPathNodes *broken_node =
          CR_RegionAlloc(ctx->r, sizeof(*broken_node));
        broken_node->node = node;
        broken_node->next = ctx->broken_nodes;
        ctx->broken_nodes = broken_node;
        break;
      }
    }
    checkIntegrityRecursively(ctx, node->subnodes, check_file_callback);
  }
}

static CR_Region *attachDisposableRegion(IntegrityCheckContext *ctx)
{
  CR_Region *disposable_r = CR_RegionNew();
  ctx->reusable_buffer_allocator =
    allocatorWrapOneSingleGrowableBuffer(disposable_r);
  ctx->unique_subpath_buffer = CR_RegionAllocGrowable(disposable_r, 1);
  ctx->unique_subpath_cache = strTableNew(disposable_r);
  ctx->unique_subpath_cache_key_allocator =
    allocatorWrapRegion(disposable_r);
  return disposable_r;
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
ListOfBrokenPathNodes *checkIntegrity(CR_Region *r,
                                      const Metadata *metadata,
                                      StringView repo_path)
{
  IntegrityCheckContext ctx = {
    .r = r,
    .broken_nodes = NULL,
    .repo_path = repo_path,
  };

  /* Dry run to populate `ctx->files_to_hash_total_size`. */
  CR_Region *disposable_r = attachDisposableRegion(&ctx);
  checkIntegrityRecursively(&ctx, metadata->paths, addToTotalFileSize);
  CR_RegionRelease(disposable_r);
  disposable_r = attachDisposableRegion(&ctx);

  /* Do a real check. */
  checkIntegrityRecursively(&ctx, metadata->paths, storedFileIsHealthy);
  CR_RegionRelease(disposable_r);

  return ctx.broken_nodes;
}
