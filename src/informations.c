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
  Implements printing of various backup informations.
*/

#include "informations.h"

#include <stdio.h>

#include "colors.h"
#include "safe-wrappers.h"

/** Prints the given config line number in colors to stderr. */
static void warnConfigLineNr(size_t line_nr)
{
  colorPrintf(stderr, TC_yellow, "config");
  fprintf(stderr, ": ");
  colorPrintf(stderr, TC_blue, "line ");
  colorPrintf(stderr, TC_red, "%zu", line_nr);
  fprintf(stderr, ": ");
}

/** Prints the given path in quotes to stderr, colorized and followed by a
  newline. */
static void warnPathNewline(String path)
{
  fprintf(stderr, "\"");
  colorPrintf(stderr, TC_red, "%s", path.str);
  fprintf(stderr, "\"\n");
}

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
      warnConfigLineNr(node->line_nr);
      fprintf(stderr, "%s never matched a %s: ",
              node->regex?"regex":"string",
              node->subnodes?"directory":"file");
      warnPathNewline(node->name);
    }
    else if(node->subnodes != NULL)
    {
      if(!(node->search_match & SRT_directory))
      {
        warnConfigLineNr(node->line_nr);
        fprintf(stderr, "%s matches, but not a directory: ",
                node->regex?"regex":"string");
        warnPathNewline(node->name);
      }
      else if(node->search_match & ~SRT_directory)
      {
        warnConfigLineNr(node->line_nr);
        fprintf(stderr, "%s matches not only directories: ",
                node->regex?"regex":"string");
        warnPathNewline(node->name);
        printSearchNodeInfos(node);
      }
      else
      {
        printSearchNodeInfos(node);
      }
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
  colorPrintf(stdout, TC_green_bold, "++ ");
  colorPrintf(stdout, TC_green, "%s/", node->path.str);
  printf(" (");

  if(changes.new_items_count > 0)
  {
    printf("+%zu item%s", changes.new_items_count,
           changes.new_items_count == 1?"":"s");
    if(changes.new_files_size > 0)
    {
      printf(", +");
      printHumanReadableSize(changes.new_files_size);
    }
  }
  else
  {
    printf("Empty");
  }

  printf(")\n");
}

/** Prints changes in the given metadata tree recursively.

  @param metadata A metadata tree which must have been initiated with
  initiateBackup().
  @param path_list The path list into which should be recursed.
  @param print True, if informations should be printed.

  @return A shallow summary of the printed changes for further processing.
*/
static MetadataChanges printPathListRecursively(Metadata *metadata,
                                                PathNode *path_list,
                                                bool print)
{
  MetadataChanges changes =
  {
    .new_items_count = 0,
    .new_files_size = 0,
  };

  for(PathNode *node = path_list; node != NULL; node = node->next)
  {
    /* Skip files that already existed in the last backup. */
    if(node->history->backup != &metadata->current_backup ||
       node->history->next != NULL)
    {
      continue;
    }
    /* Skip non-directories without a policy. */
    else if(node->history->state.type != PST_directory &&
            node->policy == BPOL_none)
    {
      continue;
    }

    if(node->history->state.type == PST_regular)
    {
      changes.new_items_count = sSizeAdd(changes.new_items_count, 1);
      changes.new_files_size =
        sUint64Add(changes.new_files_size,
                   node->history->state.metadata.reg.size);

      if(print)
      {
        colorPrintf(stdout, TC_green_bold, "++ ");
        colorPrintf(stdout, TC_green, "%s\n", node->path.str);
      }
    }
    else if(node->history->state.type == PST_symlink)
    {
      changes.new_items_count = sSizeAdd(changes.new_items_count, 1);

      if(print)
      {
        colorPrintf(stdout, TC_green_bold, "++ ");
        colorPrintf(stdout, TC_cyan, "%s", node->path.str);
        colorPrintf(stdout, TC_magenta, " -> ");
        colorPrintf(stdout, TC_yellow, "%s\n",
                    node->history->state.metadata.sym_target);
      }
    }
    else if(node->history->state.type == PST_directory)
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

      if(node->policy != BPOL_none || subnode_changes.new_items_count > 0)
      {
        changes.new_items_count = sSizeAdd(changes.new_items_count, 1);
      }

      changes.new_items_count =
        sSizeAdd(changes.new_items_count, subnode_changes.new_items_count);
      changes.new_files_size =
        sUint64Add(changes.new_files_size, subnode_changes.new_files_size);
    }
  }

  return changes;
}

/** Prints the given size in a human readable way.

  @param size The size which should be printed.
*/
void printHumanReadableSize(uint64_t size)
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
    printf("%.0lf b", converted_value);
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
      warnConfigLineNr(expression->line_nr);
      fprintf(stderr, "regex never matched a path: ");
      warnPathNewline(expression->expression);
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
