/** @file
  Implements printing of various backup informations.
*/

#include "informations.h"

#include <math.h>
#include <stdio.h>

#include "colors.h"
#include "safe-math.h"
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

/** Prints the given path in quotes to stderr, colorized. */
static void warnPath(String path)
{
  fprintf(stderr, "\"");
  colorPrintf(stderr, TC_red, "%s", path.content);
  fprintf(stderr, "\"");
}

/** Prints the given path in quotes to stderr, colorized and followed by a
  newline. */
static void warnPathNewline(String path)
{
  warnPath(path);
  fputc('\n', stderr);
}

/** Prints every expression in the given list which has never matched a
  string. */
static void warnUnmatchedExpressions(RegexList *expression_list,
                                     const char *target_name)
{
  for(RegexList *expression = expression_list;
      expression != NULL; expression = expression->next)
  {
    if(expression->has_matched == false)
    {
      warnConfigLineNr(expression->line_nr);
      fprintf(stderr, "regex never matched a %s: ", target_name);
      warnPathNewline(expression->expression);
    }
  }
}

/** Returns either "regex" or "string" depending on the given node. */
static const char *typeOf(SearchNode *node)
{
  if(node->regex != NULL)
  {
    return "regex";
  }
  else
  {
    return "string";
  }
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
      fprintf(stderr, "%s never matched a %s: ", typeOf(node),
              node->subnodes?"directory":"file");
      warnPathNewline(node->name);
    }
    else if(node->subnodes != NULL)
    {
      if(!(node->search_match & SRT_directory))
      {
        warnConfigLineNr(node->line_nr);
        fprintf(stderr, "%s matches, but not a directory: ", typeOf(node));
        warnPathNewline(node->name);
      }
      else if(node->search_match & ~SRT_directory)
      {
        warnConfigLineNr(node->line_nr);
        fprintf(stderr, "%s matches not only directories: ", typeOf(node));
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

  changeStatsAdd(&a->lost_items,
                 b.lost_items.count,
                 b.lost_items.size);

  changeStatsAdd(&a->changed_items,
                 b.changed_items.count,
                 b.changed_items.size);

  a->other |= b.other;
}

/** Returns the first path state in the given nodes history. If this path
  state represents a non-existing file and its predecessor exists, it will
  return the predecessor. */
static PathState *getExistingState(PathNode *node)
{
  if(node->history->state.type == PST_non_existing &&
     node->history->next != NULL)
  {
    return &node->history->next->state;
  }
  else
  {
    return &node->history->state;
  }
}

/** Adds statistics about the given nodes current change type to the
  specified change structure.

  @param node The node to consider.
  @param changes The change struct which should be updated.
*/
static void addNode(PathNode *node, MetadataChanges *changes)
{
  BackupHint hint = backupHintNoPol(node->hint);
  PathState *state = getExistingState(node);
  uint64_t size = 0;

  if(state->type == PST_regular)
  {
    size = state->metadata.reg.size;
  }

  if(hint == BH_added)
  {
    changeStatsAdd(&changes->new_items, 1, size);
    changes->affects_parent_timestamp = true;
  }
  else if(hint == BH_removed)
  {
    changeStatsAdd(&changes->removed_items, 1, size);
    changes->affects_parent_timestamp = true;
  }
  else if(hint == BH_not_part_of_repository)
  {
    changeStatsAdd(&changes->lost_items, 1, size);

    if(node->policy == BPOL_mirror &&
       (node->hint & BH_policy_changed) == false)
    {
      changes->affects_parent_timestamp = true;
    }
  }
  else if(hint & BH_content_changed)
  {
    changeStatsAdd(&changes->changed_items, 1, size);
    changes->affects_parent_timestamp = true;
  }
  else if(node->hint > BH_unchanged &&
          (node->policy != BPOL_none ||
           (node->hint < BH_owner_changed ||
            node->hint > BH_timestamp_changed)))
  {
    changes->other = true;
    changes->affects_parent_timestamp |=
      (hint >= BH_regular_to_symlink &&
       hint <= BH_other_to_directory);
  }
}

/** Returns true if the given struct contains any non-metadata related
  changes. */
static bool containsContentChanges(MetadataChanges changes)
{
  return
    changes.new_items.count > 0 ||
    changes.removed_items.count > 0 ||
    changes.lost_items.count > 0 ||
    changes.changed_items.count > 0;
}

/** Prints the informations in the given change stats.

  @param stats The struct containing the informations.
  @param prefix The string to print before each number larger than 0.
*/
static void printChangeStats(ChangeStats stats, const char *prefix)
{
  printf("%s%zu item%s", stats.count > 0? prefix:"",
         stats.count, stats.count == 1? "":"s");

  if(stats.size > 0)
  {
    printf(", %s", prefix);
    printHumanReadableSize(stats.size);
  }
}

/** Prints an opening paren on its first call and a comma on all other
  invocations of this function.

  @param printed_prefix Stores the data about whether this function was
  called already or not. Will be updated by this function.
*/
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
  PathState *state = getExistingState(node);

  colorPrintf(stdout, color, "%s%s%s", state->type == PST_symlink? "^":"",
              node->path.content, state->type == PST_directory? "/":"");
}

/** Prints informations about the given node.

  @param node The node to consider.
  @param subnode_changes Statistics gathered from all the current nodes
  subnodes.
*/
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
          hint <= BH_other_to_directory)
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
     hint <= BH_other_to_directory)
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
      case BH_other_to_regular:     printf("Other -> File");        break;
      case BH_other_to_symlink:     printf("Other -> Symlink");     break;
      case BH_other_to_directory:   printf("Other -> Directory");   break;
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

  if(node->history->state.type != PST_symlink &&
     !(node->hint & BH_timestamp_changed) !=
     !(node->hint & BH_content_changed))
  {
    printPrefix(&printed_details);
    printf("%stimestamp", (node->hint & BH_timestamp_changed)? "":"same ");
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

  PathState *existing_state = getExistingState(node);
  if(existing_state->type == PST_directory)
  {
    if(hint == BH_added ||
       hint == BH_regular_to_directory ||
       hint == BH_symlink_to_directory)
    {
      printPrefix(&printed_details);
      printChangeStats(subnode_changes.new_items, "+");
    }
    else if(hint == BH_removed)
    {
      printPrefix(&printed_details);
      printChangeStats(subnode_changes.removed_items, "-");
    }
    else if(hint == BH_not_part_of_repository)
    {
      printPrefix(&printed_details);
      printChangeStats(subnode_changes.lost_items, "-");
    }
  }
  else if(hint == BH_directory_to_regular ||
          hint == BH_directory_to_symlink)
  {
    ChangeStats lost_files = { .count = 0, .size = 0 };
    changeStatsAdd(&lost_files, subnode_changes.removed_items.count,
                   subnode_changes.removed_items.size);
    changeStatsAdd(&lost_files, subnode_changes.lost_items.count,
                   subnode_changes.lost_items.size);
    printPrefix(&printed_details);
    printChangeStats(lost_files, "-");
  }

  if(printed_details == true)
  {
    printf(")");
  }

  if(existing_state->type == PST_symlink)
  {
    colorPrintf(stdout, TC_magenta, " -> ");
    colorPrintf(stdout, TC_cyan, "%s",
                existing_state->metadata.sym_target.content);
  }

  printf("\n");
}

/** Check whether the given path node is being matched by an item in the
  specified regex list

  @param node Path to match.
  @param expression_list List of regular expressions. Can be NULL. The
  first expression to match will get its `has_matched` field updated.

  @return True if the given path node got matched by one of the specified
  regex patterns.
*/
static bool matchesRegexList(PathNode *node, RegexList *expression_list)
{
  for(RegexList *expression = expression_list;
      expression != NULL; expression = expression->next)
  {
    if (regexec(expression->regex, node->path.content, 0, NULL, 0) == 0)
    {
      expression->has_matched = true;
      return true;
    }
  }
  return false;
}

/** Prints informations about a tree recursively.

  @param metadata The metadata belonging to given node list.
  @param path_list A list of nodes to print informations about.
  @param summarize_expressions Optional list of regex patterns for deciding
  whether this node should be printed recursively or not. Can be NULL. May
  update the `has_matched` field in the list.
  @param print True, if informations should be printed.

  @return Statistics about all the nodes locatable trough the given path
  list.
*/
static MetadataChanges recursePrintOverTree(Metadata *metadata,
                                            PathNode *path_list,
                                            RegexList *summarize_expressions,
                                            bool print)
{
  MetadataChanges changes =
  {
    .new_items     = { .count = 0, .size = 0 },
    .removed_items = { .count = 0, .size = 0 },
    .lost_items    = { .count = 0, .size = 0 },
    .changed_items = { .count = 0, .size = 0 },
    .affects_parent_timestamp = false,
    .other = false,
  };

  for(PathNode *node = path_list; node != NULL; node = node->next)
  {
    MetadataChanges subnode_changes;

    const bool summarize =
      node->policy != BPOL_none &&
      getExistingState(node)->type == PST_directory &&
      matchesRegexList(node, summarize_expressions);
    /* Once a summarize expression matched, its subnodes should not be
       tested anymore. */
    RegexList *expressions_to_pass_down =
      summarize ? NULL : summarize_expressions;

    if(print == true && node->hint > BH_unchanged &&
       !(node->policy == BPOL_none &&
         (node->hint == BH_added ||
          (node->hint >= BH_owner_changed &&
           node->hint <= BH_timestamp_changed))))
    {
      bool print_subnodes =
        backupHintNoPol(node->hint) > BH_other_to_directory;

      subnode_changes =
        recursePrintOverTree(metadata, node->subnodes,
                             expressions_to_pass_down, print_subnodes);

      if(!(node->hint == BH_timestamp_changed &&
           subnode_changes.affects_parent_timestamp))
      {
        printNode(node, subnode_changes);
      }
    }
    else
    {
      subnode_changes =
        recursePrintOverTree(metadata, node->subnodes,
                             expressions_to_pass_down, print);
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
  double converted_value = (double)size;
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
    double decimal = 0.0;
    double fraction = floor(modf(converted_value, &decimal) * 10.0);
    printf("%.0lf.%.0lf %ciB", decimal, fraction, units[unit_index]);
  }
}

/** Prints informations about the entire given search tree.

  @param root_node The root node of the tree for which informations should
  be printed.
*/
void printSearchTreeInfos(SearchNode *root_node)
{
  printSearchNodeInfos(root_node);
  warnUnmatchedExpressions(*root_node->ignore_expressions, "path");
  warnUnmatchedExpressions(*root_node->summarize_expressions, "directory");
}

/** Prints the changes in the given metadata tree.

  @param metadata A metadata tree, which must have been initiated with
  initiateBackup().
  @param summarize_expressions Optional list of regex patters which
  describe directories that should not be printed recursively. Can be NULL.
  May update the `has_matched` field in the list.

  @return A shallow summary of the printed changes for further processing.
*/
MetadataChanges printMetadataChanges(Metadata *metadata,
                                     RegexList *summarize_expressions)
{
  return recursePrintOverTree(metadata, metadata->paths,
                              summarize_expressions, true);
}

/** Returns true if the given struct contains any changes. */
bool containsChanges(MetadataChanges changes)
{
  return containsContentChanges(changes) ||
    changes.other == true;
}

/** Prints a warning on how the specified node matches the given string. */
void warnNodeMatches(SearchNode *node, String string)
{
  warnConfigLineNr(node->line_nr);
  fprintf(stderr, "%s ", typeOf(node));
  warnPath(node->name);
  fprintf(stderr, " matches \"");
  colorPrintf(stderr, TC_yellow, "%s", string.content);
  fprintf(stderr, "\"\n");
}
