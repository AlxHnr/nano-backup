#include "garbage-collector.h"

#include <string.h>

#include "CRegion/region.h"

#include "path-builder.h"
#include "safe-math.h"
#include "safe-wrappers.h"
#include "string-table.h"

static char *path_buffer = NULL;

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
    StringView path = strWrap(buffer);

    if(strTableGet(table, path) == NULL)
    {
      strTableMap(table, strCopy(path), (void *)0x1);
    }
  }

  for(const PathNode *subnode = node->subnodes; subnode != NULL;
      subnode = subnode->next)
  {
    populateTableRecursively(table, subnode);
  }
}

/** Recurses into the directory stored in path_buffer and removes
  everything not mapped in the given table.

  @param table The table containing referenced file paths.
  @param length The length of the path in path_buffer.
  @param repo_path_length The length of the path to the repository.
  @param gc_stats GC statistics which will be incremented by this function.

  @return True if the directory specified in path_buffer contains
  referenced files.
*/
static bool recurseIntoDirectory(const StringTable *table,
                                 const size_t length,
                                 const size_t repo_path_length,
                                 GCStatistics *gc_stats)
{
  bool item_required = length == repo_path_length;
  const struct stat stats = length == repo_path_length
    ? sStat(strWrap(path_buffer))
    : sLStat(strWrap(path_buffer));

  if(S_ISDIR(stats.st_mode))
  {
    DIR *dir = sOpenDir(strWrap(path_buffer));

    for(struct dirent *dir_entry = sReadDir(dir, strWrap(path_buffer));
        dir_entry != NULL; dir_entry = sReadDir(dir, strWrap(path_buffer)))
    {
      const size_t sub_path_length =
        pathBuilderAppend(&path_buffer, length, dir_entry->d_name);

      item_required |= recurseIntoDirectory(table, sub_path_length,
                                            repo_path_length, gc_stats);

      path_buffer[length] = '\0';
    }

    sCloseDir(dir, strWrap(path_buffer));
  }
  else if(length != repo_path_length)
  {
    StringView path_in_repo = strWrapLength(
      &path_buffer[repo_path_length + 1], length - repo_path_length - 1);

    item_required |= (strTableGet(table, path_in_repo) != NULL);
  }

  if(!item_required)
  {
    sRemove(strWrap(path_buffer));
    gc_stats->deleted_items_count =
      sSizeAdd(gc_stats->deleted_items_count, 1);

    if(S_ISREG(stats.st_mode))
    {
      gc_stats->deleted_items_total_size =
        sUint64Add(gc_stats->deleted_items_total_size, stats.st_size);
    }
  }

  return item_required;
}

/** Removes unreferenced files and directories from the given repository.

  @param metadata The metadata to search for referenced files.
  @param repo_path The path to the repository which should be cleaned up.

  @return Statistics about items removed from the repository.
*/
GCStatistics collectGarbage(const Metadata *metadata, StringView repo_path)
{
  CR_Region *table_region = CR_RegionNew();
  StringTable *table = strTableNew(table_region);
  strTableMap(table, strWrap("config"), (void *)0x1);
  strTableMap(table, strWrap("metadata"), (void *)0x1);
  strTableMap(table, strWrap("lockfile"), (void *)0x1);

  for(const PathNode *node = metadata->paths; node != NULL;
      node = node->next)
  {
    populateTableRecursively(table, node);
  }

  GCStatistics gc_stats = {
    .deleted_items_count = 0,
    .deleted_items_total_size = 0,
  };

  const size_t length = pathBuilderSet(&path_buffer, repo_path.content);
  recurseIntoDirectory(table, length, length, &gc_stats);

  CR_RegionRelease(table_region);

  return gc_stats;
}
