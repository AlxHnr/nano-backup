/** @file
  Implements removing of unreferenced files from the repository.
*/

#include "garbage-collector.h"

#include <string.h>

#include "path-builder.h"
#include "string-table.h"
#include "safe-math.h"
#include "safe-wrappers.h"

static Buffer *path_buffer = NULL;

/** Populates the given StringTable with referenced unique filepaths
  relative to their repository.

  @param table The table to populate.
  @param node The node to recurse into.
*/
static void populateTableRecursively(StringTable *table, PathNode *node)
{
  if(backupHintNoPol(node->hint) == BH_not_part_of_repository)
  {
    return;
  }

  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->state.type != PST_regular ||
       point->state.metadata.reg.size <= FILE_HASH_SIZE)
    {
      continue;
    }

    repoBuildRegularFilePath(&path_buffer, &point->state.metadata.reg);
    String path = strWrap(path_buffer->data);

    if(strTableGet(table, path) == NULL)
    {
      strTableMap(table, strCopy(path), (void *)0x1);
    }
  }

  for(PathNode *subnode = node->subnodes;
      subnode != NULL; subnode = subnode->next)
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
static bool recurseIntoDirectory(StringTable *table, size_t length,
                                 size_t repo_path_length,
                                 GCStats *gc_stats)
{
  bool item_required = length == repo_path_length;
  struct stat stats =
    length == repo_path_length?
    sStat(strWrap(path_buffer->data)):
    sLStat(strWrap(path_buffer->data));

  if(S_ISDIR(stats.st_mode))
  {
    DIR *dir = sOpenDir(strWrap(path_buffer->data));

    for(struct dirent *dir_entry = sReadDir(dir, strWrap(path_buffer->data));
        dir_entry != NULL; dir_entry = sReadDir(dir, strWrap(path_buffer->data)))
    {
      size_t sub_path_length =
        pathBuilderAppend(&path_buffer, length, dir_entry->d_name);

      item_required |= recurseIntoDirectory(table, sub_path_length,
                                            repo_path_length, gc_stats);

      path_buffer->data[length] = '\0';
    }

    sCloseDir(dir, strWrap(path_buffer->data));
  }
  else if(length != repo_path_length)
  {
    String path_in_repo =
      (String)
      {
        .content = &path_buffer->data[repo_path_length + 1],
        .length = length - repo_path_length - 1,
      };

    item_required |= (strTableGet(table, path_in_repo) != NULL);
  }

  if(item_required == false)
  {
    sRemove(strWrap(path_buffer->data));
    gc_stats->count = sSizeAdd(gc_stats->count, 1);

    if(S_ISREG(stats.st_mode))
    {
      gc_stats->size = sUint64Add(gc_stats->size, stats.st_size);
    }
  }

  return item_required;
}

/** Removes unreferenced files and directories from the given repository.

  @param metadata The metadata to search for referenced files.
  @param repo_path The path to the repository which should be cleaned up.

  @return Statistics about items removed from the repository.
*/
GCStats collectGarbage(Metadata *metadata, String repo_path)
{
  StringTable *table = strTableNew();
  strTableMap(table, strWrap("config"),   (void *)0x1);
  strTableMap(table, strWrap("metadata"), (void *)0x1);

  for(PathNode *node = metadata->paths; node != NULL; node = node->next)
  {
    populateTableRecursively(table, node);
  }

  GCStats gc_stats = { .count = 0, .size = 0 };

  size_t length = pathBuilderSet(&path_buffer, repo_path.content);
  recurseIntoDirectory(table, length, length, &gc_stats);

  strTableFree(table);

  return gc_stats;
}
