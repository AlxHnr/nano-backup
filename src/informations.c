/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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
  Implements printing of various backup informations.
*/

#include "informations.h"

#include <stdio.h>

#include "safe-wrappers.h"

/** Recursively prints informations about all nodes in the given search
  tree, that have never matched an existing file or directory.

  @param root_node The root node of the search tree, for which the
  informations should be printed.
*/
static void printSearchNodeInfos(SearchNode *root_node)
{
  for(SearchNode *node = root_node->subnodes;
      node != NULL; node = node->next)
  {
    if(node->search_match == SRT_none)
    {
      printf("config: line %zu: %s never matched a %s: \"%s\"\n",
             node->line_nr, node->regex?"regex":"string",
             node->subnodes?"directory":"file", node->name.str);
    }
    else if(node->search_match != SRT_directory && node->subnodes != NULL)
    {
      printf("config: line %zu: %s matches, but not a directory: \"%s\"\n",
             node->line_nr, node->regex?"regex":"string", node->name.str);
    }
    else if(node->subnodes != NULL)
    {
      printSearchNodeInfos(node);
    }
  }
}

/** Prints informations about a new directory.

  @param node The node representing the new directory.
  @param changes A summary of all changes in the subnodes of the given
  node.
*/
static void printNewDirInfo(PathNode *node, MetadataChanges changes)
{
  printf("++ %s/ (", node->path.str);

  if(changes.new_files_count > 0)
  {
    printf("+%zu File%s, +", changes.new_files_count,
           changes.new_files_count == 1?"":"s");
    printHumanReadableSize(changes.new_files_size);
  }
  else
  {
    printf("Empty");
  }

  printf(")\n");
}

static MetadataChanges printPathListRecursively(Metadata *metadata,
                                                PathNode *path_list,
                                                bool print)
{
  MetadataChanges changes =
  {
    .new_files_count = 0,
    .new_files_size = 0
  };

  for(PathNode *node = path_list; node != NULL; node = node->next)
  {
    /* Skip files that already existed in the last backup. */
    if(node->history->backup != &metadata->current_backup ||
       node->history->next != NULL)
    {
      continue;
    }

    if(node->history->state.type == PST_directory)
    {
      MetadataChanges subnode_changes;

      if(node->policy != BPOL_none && print == true)
      {
        subnode_changes =
          printPathListRecursively(metadata, node->subnodes, false);
        printNewDirInfo(node, subnode_changes);
      }
      else
      {
        subnode_changes =
          printPathListRecursively(metadata, node->subnodes, print);
      }

      changes.new_files_count =
        sUint64Add(changes.new_files_count,
                   subnode_changes.new_files_count);
      changes.new_files_size =
        sSizeAdd(changes.new_files_size, subnode_changes.new_files_size);
    }
    else if(node->history->state.type == PST_regular)
    {
      changes.new_files_count = sUint64Add(changes.new_files_count, 1);
      changes.new_files_size =
        sSizeAdd(changes.new_files_size,
                 node->history->state.metadata.reg.size);

      if(print) printf("++ %s\n", node->path.str);
    }
  }

  return changes;
}

/** Prints the given size in a human readable way.

  @param size The size which should be printed.
*/
void printHumanReadableSize(size_t size)
{
  static const char units[] = "bKMGT";
  double converted_value = size;
  size_t unit_index = 0;

  while(converted_value > 999.9 && unit_index + 2 < sizeof(units))
  {
    converted_value /= 1024.0;
    unit_index++;
  }

  if(unit_index == 0)
  {
    printf("%.0lf B", converted_value);
  }
  else
  {
    printf("%.1lf %ciB", converted_value, units[unit_index]);
  }
}

/** Prints informations about the entire given search tree.

  @param root_node The root node of the tree for which informations should
  be printed.
*/
void printSearchTreeInfos(SearchNode *root_node)
{
  printSearchNodeInfos(root_node);

  for(RegexList *expression = *root_node->ignore_expressions;
      expression != NULL; expression = expression->next)
  {
    if(expression->has_matched == false)
    {
      printf("config: line %zu: regex never matched a path: \"%s\"\n",
             expression->line_nr, expression->expression.str);
    }
  }
}

/** Prints the changes in the given metadata tree.

  @param metadata A metadata tree, which must have been initiated with
  initiateBackup().

  @return A shallow summary of the printed changes for further processing.
*/
MetadataChanges printMetadataChanges(Metadata *metadata)
{
  return printPathListRecursively(metadata, metadata->paths, true);
}