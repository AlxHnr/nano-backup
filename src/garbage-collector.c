#include "garbage-collector.h"

#include <string.h>

#include "CRegion/alloc-growable.h"
#include "CRegion/region.h"
#include "allocator.h"
#include "safe-math.h"
#include "safe-wrappers.h"
#include "string-table.h"

/** Populates the given StringTable with referenced unique filepaths
  relative to their repository.

  @param table The table to populate.
  @param node The node to recurse into.
*/
static void populateTableRecursively(StringTable *table,
                                     const PathNode *node)
{
  if(backupHintNoPol(node->hint) == BH_not_part_of_repository)
  {
    return;
  }

  for(const PathHistory *point = node->history; point != NULL;
      point = point->next)
  {
    if(point->state.type != PST_regular_file ||
       point->state.metadata.file_info.size <= FILE_HASH_SIZE)
    {
      continue;
    }

    static char *buffer = NULL;
    repoBuildRegularFilePath(&buffer, &point->state.metadata.file_info);
    StringView path = str(buffer);

    if(strTableGet(table, path) == NULL)
    {
      strTableMap(table, strLegacyCopy(path), (void *)0x1);
    }
  }

  for(const PathNode *subnode = node->subnodes; subnode != NULL;
      subnode = subnode->next)
  {
    populateTableRecursively(table, subnode);
  }
}

/** Used during a garbage collection run. */
typedef struct
{
  StringView repo_path;

  /** All file paths inside this table are relative to repo_path. */
  StringTable *paths_to_preserve;

  /** Populated with informations during garbage collection. */
  GCStatistics statistics;
} GCContext;

static bool shouldBeRemoved(StringView path, const struct stat *stats,
                            void *user_data)
{
  GCContext *ctx = user_data;

  if(strEqual(path, ctx->repo_path))
  {
    return false;
  }
  StringView path_relative_to_repo =
    strUnterminated(&path.content[ctx->repo_path.length + 1],
                    path.length - ctx->repo_path.length - 1);
  if(strTableGet(ctx->paths_to_preserve, path_relative_to_repo) != NULL)
  {
    return false;
  }

  ctx->statistics.deleted_items_count =
    sSizeAdd(ctx->statistics.deleted_items_count, 1);
  if(S_ISREG(stats->st_mode))
  {
    ctx->statistics.deleted_items_total_size =
      sSizeAdd(ctx->statistics.deleted_items_total_size, stats->st_size);
  }
  return true;
}

/** Removes unreferenced files and directories from the given repository.

  @param metadata The metadata to search for referenced files.
  @param repo_path The path to the repository which should be cleaned up.

  @return Statistics about items removed from the repository.
*/
GCStatistics collectGarbage(const Metadata *metadata, StringView repo_path)
{
  CR_Region *r = CR_RegionNew();

  GCContext ctx = {
    .repo_path = repo_path,
    .paths_to_preserve = strTableNew(r),
  };
  strTableMap(ctx.paths_to_preserve, str("config"), (void *)0x1);
  strTableMap(ctx.paths_to_preserve, str("metadata"), (void *)0x1);
  strTableMap(ctx.paths_to_preserve, str("lockfile"), (void *)0x1);

  for(const PathNode *node = metadata->paths; node != NULL;
      node = node->next)
  {
    populateTableRecursively(ctx.paths_to_preserve, node);
  }

  DirIterator *dir = sDirOpen(repo_path);
  for(StringView subpath = sDirGetNext(dir); subpath.length > 0;
      strSet(&subpath, sDirGetNext(dir)))
  {
    sRemoveRecursivelyIf(subpath, shouldBeRemoved, &ctx);
  }
  sDirClose(dir);

  CR_RegionRelease(r);
  return ctx.statistics;
}
