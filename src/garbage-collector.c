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
  Implements removing of unreferenced files from the repository.
*/

#include "garbage-collector.h"

#include <string.h>

#include "path-builder.h"
#include "string-table.h"
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
    String path = str(path_buffer->data);

    if(strtableGet(table, path) == NULL)
    {
      strtableMap(table, strCopy(path), (void *)0x1);
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

  @return True if the directory specified in path_buffer contains
  referenced files.
*/
static bool recurseIntoDirectory(StringTable *table, size_t length,
                                 size_t repo_path_length)
{
  bool item_required = false;
  struct stat stats = sLStat(path_buffer->data);

  if(S_ISDIR(stats.st_mode))
  {
    DIR *dir = sOpenDir(path_buffer->data);

    for(struct dirent *dir_entry = sReadDir(dir, path_buffer->data);
        dir_entry != NULL; dir_entry = sReadDir(dir, path_buffer->data))
    {
      size_t sub_path_length =
        pathBuilderAppend(&path_buffer, length, dir_entry->d_name);

      item_required |=
        recurseIntoDirectory(table, sub_path_length, repo_path_length);

      path_buffer->data[length] = '\0';
    }

    sCloseDir(dir, path_buffer->data);
  }
  else
  {
    String path_in_repo =
      (String)
      {
        .str = &path_buffer->data[repo_path_length + 1],
        .length = length - repo_path_length - 1,
      };

    if(strtableGet(table, path_in_repo) != NULL)
    {
      item_required = true;
    }
  }

  if(item_required == false)
  {
    sRemove(path_buffer->data);
  }

  return item_required;
}

/** Removes unreferenced files and directories from the given repository.

  @param metadata The metadata to search for referenced files.
  @param repo_path The path to the repository which should be cleaned up.
*/
void collectGarbage(Metadata *metadata, const char *repo_path)
{
  StringTable *table = strtableNew();
  strtableMap(table, str("config"),   (void *)0x1);
  strtableMap(table, str("metadata"), (void *)0x1);

  for(PathNode *node = metadata->paths; node != NULL; node = node->next)
  {
    populateTableRecursively(table, node);
  }

  size_t length = pathBuilderSet(&path_buffer, repo_path);
  recurseIntoDirectory(table, length, length);

  strtableFree(table);
}
