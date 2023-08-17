#ifndef NANO_BACKUP_SRC_SEARCH_TREE_H
#define NANO_BACKUP_SRC_SEARCH_TREE_H

#include <regex.h>
#include <stdbool.h>

#include "CRegion/region.h"
#include "backup-policies.h"
#include "search-result-type.h"
#include "str.h"

typedef struct RegexList RegexList;
struct RegexList
{
  /** The expression in compiled form. */
  const regex_t *regex;

  /** The uncompiled regular expression as a string. */
  StringView expression;

  /** The number of the line in the config file on which this expression
    was defined. */
  size_t line_nr;

  /** True, if this expression matched anything in its lifetime. */
  bool has_matched;

  RegexList *next;
};

typedef struct SearchNode SearchNode;
struct SearchNode
{
  /** The name or expression of the node. */
  StringView name;

  /** The number of the line in the config file on which this node appeared
    initially. This may not be the line on which this node got a policy
    assigned to it. */
  size_t line_nr;

  /** If this value is not NULL, it contains a compiled regex and will be
    used for matching files. */
  const regex_t *regex;

  /** Contains the type of the file that this node has matched during a
    search. */
  SearchResultType search_match;

  /** The backup policy for this node. */
  BackupPolicy policy;

  /** True, if the policy was inherited by the parent node. Otherwise this
    node got its own policy assigned to it in the config file. */
  bool policy_inherited;

  /** The number of the line in the config file, on which the policy for
    the current node was set. */
  size_t policy_line_nr;

  /** A pointer to the first subnode, or NULL. */
  SearchNode *subnodes;

  /** True, if at least one subnode contains a regular expression. Subnodes
    of the subnode do not influence this variable. */
  bool subnodes_contain_regex;

  /** Points to the search trees common ignore expression list, which is
    shared across all of its nodes. This allows starting a search with
    every node. It matches filepaths that should be ignored. The common
    ignore expression list can point to NULL if its empty. */
  RegexList **ignore_expressions;

  /** Points to the search trees shared summarize expression list. It
    matches directories which should not be printed recursively during a
    backup. Can point to NULL if its empty. */
  RegexList **summarize_expressions;

  SearchNode *next;
};

extern SearchNode *searchTreeParse(CR_Region *r, StringView config);
extern SearchNode *searchTreeLoad(CR_Region *r, StringView path_to_config);

#endif
