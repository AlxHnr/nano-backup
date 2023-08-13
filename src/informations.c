#include "informations.h"

#include <inttypes.h>
#include <stdio.h>

#include "colors.h"
#include "safe-math.h"
#include "safe-wrappers.h"

static void warnConfigLineNr(const size_t line_nr)
{
  colorPrintf(stderr, TC_yellow, "config");
  fprintf(stderr, ": ");
  colorPrintf(stderr, TC_blue, "line ");
  colorPrintf(stderr, TC_red, "%zu", line_nr);
  fprintf(stderr, ": ");
}

static void warnPath(StringView path)
{
  fprintf(stderr, "\"");
  colorPrintf(stderr, TC_red, "%s", path.content);
  fprintf(stderr, "\"");
}

static void warnPathNewline(StringView path)
{
  warnPath(path);
  fputc('\n', stderr);
}

static void warnUnmatchedExpressions(const RegexList *expression_list,
                                     const char *target_name)
{
  for(const RegexList *expression = expression_list; expression != NULL;
      expression = expression->next)
  {
    if(!expression->has_matched)
    {
      warnConfigLineNr(expression->line_nr);
      fprintf(stderr, "regex never matched a %s: ", target_name);
      warnPathNewline(expression->expression);
    }
  }
}

static const char *typeOf(const SearchNode *node)
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
static void printSearchNodeInfos(const SearchNode *root_node)
{
  for(const SearchNode *node = root_node->subnodes; node != NULL;
      node = node->next)
  {
    if(node->search_match == SRT_none)
    {
      warnConfigLineNr(node->line_nr);
      fprintf(stderr, "%s never matched a %s: ", typeOf(node),
              node->subnodes ? "directory" : "file");
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

static void changeDetailAdd(ChangeDetail *details, const size_t count,
                            const uint64_t size)
{
  details->affected_items_count =
    sSizeAdd(details->affected_items_count, count);
  details->affected_items_total_size =
    sUint64Add(details->affected_items_total_size, size);
}

static void changeSummaryAdd(ChangeSummary *a, const ChangeSummary *b)
{
  changeDetailAdd(&a->new_items, b->new_items.affected_items_count,
                  b->new_items.affected_items_total_size);
  changeDetailAdd(&a->removed_items, b->removed_items.affected_items_count,
                  b->removed_items.affected_items_total_size);
  changeDetailAdd(&a->lost_items, b->lost_items.affected_items_count,
                  b->lost_items.affected_items_total_size);
  changeDetailAdd(&a->changed_items, b->changed_items.affected_items_count,
                  b->changed_items.affected_items_total_size);
  a->changed_attributes =
    sSizeAdd(a->changed_attributes, b->changed_attributes);
  a->other_changes_exist |= b->other_changes_exist;
}

/** Returns the first path state in the given nodes history. If this path
  state represents a non-existing file and its predecessor exists, it will
  return the predecessor. */
static const PathState *getExistingState(const PathNode *node)
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

/** Increment the attribute counter in the given change struct based on the
  specified hint. */
static void incrementExtraChangedAttributes(ChangeSummary *changes,
                                            const BackupHint hint)
{
  if(hint & BH_owner_changed)
  {
    changes->changed_attributes = sSizeAdd(changes->changed_attributes, 1);
  }
  if(hint & BH_permissions_changed)
  {
    changes->changed_attributes = sSizeAdd(changes->changed_attributes, 1);
  }
}

/** Adds statistics about the given nodes current change type to the
  specified change structure.

  @param node The node to consider.
  @param changes The change struct which should be updated.
  @param timestamp_changed_by_subnodes True if this nodes timestamp change
  was caused by a subnode.
*/
static void addNode(const PathNode *node, ChangeSummary *changes,
                    const bool timestamp_changed_by_subnodes)
{
  const BackupHint hint = backupHintNoPol(node->hint);
  const PathState *state = getExistingState(node);
  uint64_t size = 0;

  if(state->type == PST_regular_file)
  {
    size = state->metadata.file_info.size;
  }

  if(hint == BH_added)
  {
    changeDetailAdd(&changes->new_items, 1, size);
    changes->affects_parent_timestamp = true;
  }
  else if(hint == BH_removed)
  {
    changeDetailAdd(&changes->removed_items, 1, size);
    changes->affects_parent_timestamp = true;
  }
  else if(hint == BH_not_part_of_repository)
  {
    changeDetailAdd(&changes->lost_items, 1, size);

    if(node->policy == BPOL_mirror && !(node->hint & BH_policy_changed))
    {
      changes->affects_parent_timestamp = true;
    }
  }
  else if(hint & BH_content_changed)
  {
    changeDetailAdd(&changes->changed_items, 1, size);
    changes->affects_parent_timestamp = true;
    incrementExtraChangedAttributes(changes, hint);
  }
  else if(node->hint > BH_unchanged &&
          (node->policy != BPOL_none ||
           (node->hint < BH_owner_changed ||
            node->hint > BH_timestamp_changed)))
  {
    changes->other_changes_exist = true;
    changes->affects_parent_timestamp |=
      (hint >= BH_regular_to_symlink && hint <= BH_other_to_directory);

    incrementExtraChangedAttributes(changes, node->hint);
    if(node->hint == BH_timestamp_changed &&
       !timestamp_changed_by_subnodes)
    {
      changes->changed_attributes =
        sSizeAdd(changes->changed_attributes, 1);
    }
  }
}

/** Returns true if the given struct contains any non-metadata related
  changes. */
static bool containsContentChanges(const ChangeSummary *changes)
{
  return changes->new_items.affected_items_count > 0 ||
    changes->removed_items.affected_items_count > 0 ||
    changes->lost_items.affected_items_count > 0 ||
    changes->changed_items.affected_items_count > 0;
}

/** Prints the informations in the given change details.

  @param details The struct containing the informations.
  @param prefix The string to print before each number larger than 0.
*/
static void printChangeDetail(const ChangeDetail details,
                              const char *prefix)
{
  printf("%s%zu item%s", details.affected_items_count > 0 ? prefix : "",
         details.affected_items_count,
         details.affected_items_count == 1 ? "" : "s");

  if(details.affected_items_total_size > 0)
  {
    printf(", %s", prefix);
    printHumanReadableSize(details.affected_items_total_size);
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

/** Print a summary of all changes in subnodes.

  @param summary Contains the changes to print.
  @param printed_prefix See printPrefix().
*/
static void printSummarizedDetail(const ChangeSummary *summary,
                                  bool *printed_prefix)
{
  if(summary->new_items.affected_items_count > 0)
  {
    printPrefix(printed_prefix);
    printChangeDetail(summary->new_items, "+");
  }

  ChangeDetail deleted_items = summary->removed_items;
  changeDetailAdd(&deleted_items, summary->lost_items.affected_items_count,
                  summary->lost_items.affected_items_total_size);
  if(deleted_items.affected_items_count > 0)
  {
    printPrefix(printed_prefix);
    printChangeDetail(deleted_items, "-");
  }
  if(summary->changed_items.affected_items_count > 0)
  {
    printPrefix(printed_prefix);
    printf("%zu changed", summary->changed_items.affected_items_count);
  }
  if(summary->changed_attributes > 0)
  {
    printPrefix(printed_prefix);
    printf("%zu metadata change%s", summary->changed_attributes,
           summary->changed_attributes == 1 ? "" : "s");
  }
}

static void printNodePath(const PathNode *node, const TextColor color)
{
  const PathState *state = getExistingState(node);

  colorPrintf(stdout, color, "%s%s%s",
              state->type == PST_symlink ? "^" : "", node->path.content,
              state->type == PST_directory ? "/" : "");
}

/** Prints informations about the given node.

  @param node The node to consider.
  @param summary Statistics gathered from all the current nodes subnodes.
  @param summarize_subnode_changes True if the subnode changes should be
  printed in a concise way.
*/
static void printNode(const PathNode *node, const ChangeSummary *summary,
                      const bool summarize_subnode_changes)
{
  const BackupHint hint = backupHintNoPol(node->hint);

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
  else if(hint >= BH_regular_to_symlink && hint <= BH_other_to_directory)
  {
    colorPrintf(stdout, TC_cyan_bold, "<> ");
    printNodePath(node, TC_cyan);
  }
  else if(hint & BH_content_changed)
  {
    colorPrintf(stdout, TC_yellow_bold, "!! ");
    printNodePath(node, TC_yellow);
  }
  else if(summarize_subnode_changes && containsContentChanges(summary))
  {
    colorPrintf(stdout, TC_yellow_bold, "!! ");
    printNodePath(node, TC_yellow);
    printf("...");
  }
  else if(hint != BH_none)
  {
    colorPrintf(stdout, TC_magenta_bold, "@@ ");
    printNodePath(node, TC_magenta);

    if(summarize_subnode_changes && summary->changed_attributes > 0)
    {
      printf("...");
    }
  }
  else
  {
    colorPrintf(stdout, TC_blue_bold, ":: ");
    printNodePath(node, TC_blue);
  }

  bool has_printed_details = false;

  if(hint >= BH_regular_to_symlink && hint <= BH_other_to_directory)
  {
    printPrefix(&has_printed_details);

    switch(hint)
    {
      case BH_regular_to_symlink: printf("File -> Symlink"); break;
      case BH_regular_to_directory: printf("File -> Directory"); break;
      case BH_symlink_to_regular: printf("Symlink -> File"); break;
      case BH_symlink_to_directory: printf("Symlink -> Directory"); break;
      case BH_directory_to_regular: printf("Directory -> File"); break;
      case BH_directory_to_symlink: printf("Directory -> Symlink"); break;
      case BH_other_to_regular: printf("Other -> File"); break;
      case BH_other_to_symlink: printf("Other -> Symlink"); break;
      case BH_other_to_directory: printf("Other -> Directory"); break;
      default: /* ignore */ break;
    }
  }

  if(node->hint & BH_owner_changed)
  {
    printPrefix(&has_printed_details);
    printf("owner");
  }
  if(node->hint & BH_permissions_changed)
  {
    printPrefix(&has_printed_details);
    printf("permissions");
  }

  if(node->history->state.type != PST_symlink &&
     !(node->hint & BH_timestamp_changed) !=
       !(node->hint & BH_content_changed) &&
     !summary->affects_parent_timestamp)
  {
    printPrefix(&has_printed_details);
    printf("%stimestamp",
           (node->hint & BH_timestamp_changed) ? "" : "same ");
  }

  if(node->hint & BH_policy_changed)
  {
    printPrefix(&has_printed_details);
    printf("policy changed");
  }
  if(node->hint & BH_loses_history)
  {
    printPrefix(&has_printed_details);
    printf("looses history");
  }

  const PathState *existing_state = getExistingState(node);
  if(existing_state->type == PST_directory)
  {
    if(hint == BH_added || hint == BH_regular_to_directory ||
       hint == BH_symlink_to_directory)
    {
      printPrefix(&has_printed_details);
      printChangeDetail(summary->new_items, "+");
    }
    else if(hint == BH_removed)
    {
      printPrefix(&has_printed_details);
      printChangeDetail(summary->removed_items, "-");
    }
    else if(hint == BH_not_part_of_repository)
    {
      printPrefix(&has_printed_details);
      printChangeDetail(summary->lost_items, "-");
    }
    else if(summarize_subnode_changes)
    {
      printSummarizedDetail(summary, &has_printed_details);
    }
  }
  else if(hint == BH_directory_to_regular ||
          hint == BH_directory_to_symlink)
  {
    ChangeDetail lost_files = {
      .affected_items_count = 0,
      .affected_items_total_size = 0,
    };
    changeDetailAdd(&lost_files,
                    summary->removed_items.affected_items_count,
                    summary->removed_items.affected_items_total_size);
    changeDetailAdd(&lost_files, summary->lost_items.affected_items_count,
                    summary->lost_items.affected_items_total_size);
    printPrefix(&has_printed_details);
    printChangeDetail(lost_files, "-");
  }

  if(has_printed_details)
  {
    printf(")");
  }

  if(existing_state->type == PST_symlink)
  {
    colorPrintf(stdout, TC_magenta, " -> ");
    colorPrintf(stdout, TC_cyan, "%s",
                existing_state->metadata.symlink_target.content);
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
static bool matchesRegexList(const PathNode *node,
                             RegexList *expression_list)
{
  for(RegexList *expression = expression_list; expression != NULL;
      expression = expression->next)
  {
    if(regexec(expression->regex, node->path.content, 0, NULL, 0) == 0)
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
static ChangeSummary recursePrintOverTree(const Metadata *metadata,
                                          const PathNode *path_list,
                                          RegexList *summarize_expressions,
                                          const bool print)
{
  ChangeSummary changes = {
    .new_items = { .affected_items_count = 0,
                   .affected_items_total_size = 0 },
    .removed_items = { .affected_items_count = 0,
                       .affected_items_total_size = 0 },
    .lost_items = { .affected_items_count = 0,
                    .affected_items_total_size = 0 },
    .changed_items = { .affected_items_count = 0,
                       .affected_items_total_size = 0 },
    .affects_parent_timestamp = false,
    .changed_attributes = 0,
    .other_changes_exist = false,
  };

  for(const PathNode *node = path_list; node != NULL; node = node->next)
  {
    ChangeSummary summary;
    const bool summarize = node->policy != BPOL_none &&
      getExistingState(node)->type == PST_directory &&
      matchesRegexList(node, summarize_expressions);
    /* Once a summarize expression matched, its subnodes should not be
       tested anymore. */
    RegexList *expressions_to_pass_down =
      summarize ? NULL : summarize_expressions;

    if(print && summarize)
    {
      summary = recursePrintOverTree(metadata, node->subnodes,
                                     expressions_to_pass_down, false);
      if(node->hint > BH_unchanged || containsChanges(&summary))
      {
        printNode(node, &summary, summarize);
      }
    }
    else if(print && node->hint > BH_unchanged &&
            !(node->policy == BPOL_none &&
              (node->hint == BH_added ||
               (node->hint >= BH_owner_changed &&
                node->hint <= BH_timestamp_changed))))
    {
      const bool print_subnodes =
        backupHintNoPol(node->hint) > BH_other_to_directory;

      summary =
        recursePrintOverTree(metadata, node->subnodes,
                             expressions_to_pass_down, print_subnodes);

      if(!(node->hint == BH_timestamp_changed &&
           summary.affects_parent_timestamp))
      {
        printNode(node, &summary, summarize);
      }
    }
    else
    {
      summary = recursePrintOverTree(metadata, node->subnodes,
                                     expressions_to_pass_down, print);
    }

    addNode(node, &changes, summary.affects_parent_timestamp);
    changeSummaryAdd(&changes, &summary);
  }

  return changes;
}

/** Prints the given size in a human readable way.

  @param size The size which should be printed.
*/
void printHumanReadableSize(const uint64_t size)
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
    const uint64_t fraction = (uint64_t)(converted_value * 10.0) % 10;
    printf("%" PRIu64 ".%" PRIu64 " %ciB", (uint64_t)converted_value,
           fraction, units[unit_index]);
  }
}

/** Prints informations about the entire given search tree.

  @param root_node The root node of the tree for which informations should
  be printed.
*/
void printSearchTreeInfos(const SearchNode *root_node)
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
ChangeSummary printMetadataChanges(const Metadata *metadata,
                                   RegexList *summarize_expressions)
{
  return recursePrintOverTree(metadata, metadata->paths,
                              summarize_expressions, true);
}

bool containsChanges(const ChangeSummary *changes)
{
  return containsContentChanges(changes) ||
    changes->changed_attributes > 0 || changes->other_changes_exist;
}

/** Prints a warning on how the specified node matches the given string. */
void warnNodeMatches(const SearchNode *node, StringView string)
{
  warnConfigLineNr(node->line_nr);
  fprintf(stderr, "%s ", typeOf(node));
  warnPath(node->name);
  fprintf(stderr, " matches \"");
  colorPrintf(stderr, TC_yellow, "%s", string.content);
  fprintf(stderr, "\"\n");
}
