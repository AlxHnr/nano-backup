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

/** Safely adds count and size to the given stats struct. */
static void changeStatsAdd(ChangeStats *stats, size_t count, uint64_t size)
{
  stats->count = sSizeAdd(stats->count, count);
  stats->size = sUint64Add(stats->size, size);
}

/** Adds the statistics from b to a. */
static void metadataChangesAdd(MetadataChanges *a, MetadataChanges b)
{
  changeStatsAdd(&a->new_items,
                 b.new_items.count,
                 b.new_items.size);

  changeStatsAdd(&a->removed_items,
                 b.removed_items.count,
                 b.removed_items.size);

  changeStatsAdd(&a->wiped_items,
                 b.wiped_items.count,
                 b.wiped_items.size);

  changeStatsAdd(&a->changed_items,
                 b.changed_items.count,
                 b.changed_items.size);

  a->other |= b.other;
}

static void addNode(PathNode *node, MetadataChanges *changes)
{
  BackupHint hint = backupHintNoPol(node->hint);
  uint64_t size = 0;

  if(node->history->state.type == PST_regular)
  {
    size = node->history->state.metadata.reg.size;
  }
  else if(node->history->state.type == PST_non_existing &&
          node->history->next != NULL &&
          node->history->next->state.type == PST_regular)
  {
    size = node->history->next->state.metadata.reg.size;
  }

  if(hint == BH_added)
  {
    changeStatsAdd(&changes->new_items, 1, size);
  }
  else if(hint == BH_removed)
  {
    changeStatsAdd(&changes->removed_items, 1, size);
  }
  else if(hint == BH_not_part_of_repository)
  {
    changeStatsAdd(&changes->wiped_items, 1, size);
  }
  else if(hint & BH_content_changed)
  {
    changeStatsAdd(&changes->changed_items, 1, size);
  }
  else if(node->hint != BH_unchanged)
  {
    changes->other = true;
  }
}

static void printChangeStats(ChangeStats stats, const char *prefix)
{
  printf("%s%zu item%s", prefix, stats.count, stats.count == 1? "":"s");

  if(stats.size > 0)
  {
    printf(", %s", prefix);
    printHumanReadableSize(stats.size);
  }
}

static void printPrefix(bool *printed_prefix)
{
  if(*printed_prefix)
  {
    printf(", ");
  }
  else
  {
    printf(" (");
    *printed_prefix = true;
  }
}

/** Prints the given nodes path in the specified color. It will append a
  "/" if the node represents a directory. */
static void printNodePath(PathNode *node, TextColor color)
{
  colorPrintf(stdout, color, "%s%s", node->path.str,
              node->history->state.type == PST_directory? "/":"");
}

static void printNode(PathNode *node, MetadataChanges subnode_changes)
{
  BackupHint hint = backupHintNoPol(node->hint);

  if(hint == BH_added)
  {
    colorPrintf(stdout, TC_green_bold, "++ ");
    printNodePath(node, TC_green);
  }
  else if(hint == BH_removed)
  {
    colorPrintf(stdout, TC_red_bold, "-- ");
    printNodePath(node, TC_red);
  }
  else if(hint == BH_not_part_of_repository)
  {
    if(node->policy == BPOL_mirror)
    {
      colorPrintf(stdout, TC_red_bold, "xx ");
      printNodePath(node, TC_red);
    }
    else
    {
      colorPrintf(stdout, TC_blue_bold, "?? ");
      printNodePath(node, TC_blue);
    }
  }
  else if(hint >= BH_regular_to_symlink &&
          hint <= BH_directory_to_symlink)
  {
    colorPrintf(stdout, TC_cyan_bold, "<> ");
    printNodePath(node, TC_cyan);
  }
  else if(hint & BH_content_changed)
  {
    colorPrintf(stdout, TC_yellow_bold, "!! ");
    printNodePath(node, TC_yellow);
  }
  else if(hint != BH_none)
  {
    colorPrintf(stdout, TC_magenta_bold, "@@ ");
    printNodePath(node, TC_magenta);
  }
  else
  {
    colorPrintf(stdout, TC_blue_bold, ":: ");
    printNodePath(node, TC_blue);
  }

  bool printed_details = false;

  if(hint >= BH_regular_to_symlink &&
     hint <= BH_directory_to_symlink)
  {
    printPrefix(&printed_details);

    switch(hint)
    {
      case BH_regular_to_symlink:   printf("File -> Symlink");      break;
      case BH_regular_to_directory: printf("File -> Directory");    break;
      case BH_symlink_to_regular:   printf("Symlink -> File");      break;
      case BH_symlink_to_directory: printf("Symlink -> Directory"); break;
      case BH_directory_to_regular: printf("Directory -> File");    break;
      case BH_directory_to_symlink: printf("Directory -> Symlink"); break;
      default: /* ignore */ break;
    }
  }

  if(node->hint & BH_owner_changed)
  {
    printPrefix(&printed_details);
    printf("owner");
  }
  if(node->hint & BH_permissions_changed)
  {
    printPrefix(&printed_details);
    printf("permissions");
  }
  if(node->hint & BH_policy_changed)
  {
    printPrefix(&printed_details);
    printf("policy changed");
  }
  if(node->hint & BH_loses_history)
  {
    printPrefix(&printed_details);
    printf("looses history");
  }

  if(node->history->state.type == PST_directory &&
     subnode_changes.new_items.count > 0 &&
     (hint == BH_added ||
      hint == BH_regular_to_directory ||
      hint == BH_symlink_to_directory))
  {
    printPrefix(&printed_details);
    printChangeStats(subnode_changes.new_items, "+");
  }
  /* Ensure node is or was a directory. */
  else if(node->history->state.type == PST_directory ||
          (node->history->state.type == PST_non_existing &&
           node->history->next != NULL &&
           node->history->next->state.type == PST_directory))
  {
    bool has_removed_items = subnode_changes.removed_items.count > 0;
    bool has_wiped_items = subnode_changes.wiped_items.count > 0;

    if(hint == BH_removed && has_removed_items)
    {
      printPrefix(&printed_details);
      printChangeStats(subnode_changes.removed_items, "-");
    }
    else if(hint == BH_not_part_of_repository && has_wiped_items)
    {
      printPrefix(&printed_details);
      printChangeStats(subnode_changes.wiped_items, "-");
    }
    else if((hint == BH_directory_to_regular ||
             hint == BH_directory_to_symlink) &&
            (has_removed_items || has_wiped_items))
    {
      ChangeStats lost_files = { .count = 0, .size = 0 };
      changeStatsAdd(&lost_files, subnode_changes.removed_items.count,
                     subnode_changes.removed_items.size);
      changeStatsAdd(&lost_files, subnode_changes.wiped_items.count,
                     subnode_changes.wiped_items.size);
      printPrefix(&printed_details);
      printChangeStats(lost_files, "-");
    }
  }

  if(printed_details == true)
  {
    printf(")");
  }

  printf("\n");
}

static MetadataChanges recursePrintOverTree(Metadata *metadata,
                                            PathNode *path_list,
                                            bool print)
{
  MetadataChanges changes =
  {
    .new_items     = { .count = 0, .size = 0 },
    .removed_items = { .count = 0, .size = 0 },
    .wiped_items   = { .count = 0, .size = 0 },
    .changed_items = { .count = 0, .size = 0 },
    .other = false,
  };

  for(PathNode *node = path_list; node != NULL; node = node->next)
  {
    MetadataChanges subnode_changes;

    if(print == true && node->hint > BH_unchanged &&
       !(node->policy == BPOL_none && node->hint == BH_added))
    {
      bool print_subnodes =
        backupHintNoPol(node->hint) > BH_directory_to_symlink;

      subnode_changes =
        recursePrintOverTree(metadata, node->subnodes, print_subnodes);

      printNode(node, subnode_changes);
    }
    else
    {
      subnode_changes =
        recursePrintOverTree(metadata, node->subnodes, print);
    }

    addNode(node, &changes);
    metadataChangesAdd(&changes, subnode_changes);
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
  return recursePrintOverTree(metadata, metadata->paths, true);
}
