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
  Declares a struct that can be used for searching the file system.
*/

#ifndef _NANO_BACKUP_SEARCH_TREE_H_
#define _NANO_BACKUP_SEARCH_TREE_H_

#include <stdbool.h>
#include <regex.h>

#include "string-utils.h"
#include "search-result.h"
#include "backup-policies.h"

/** A list of compiled regular expressions. */
typedef struct RegexList RegexList;
struct RegexList
{
  /** The regular expression as a string. The buffer inside this string is
    guaranteed to be null-terminated. */
  String expression;

  /** The number of the line in the config file on which this expression
    was defined. */
  size_t line_nr;

  /** The expression in compiled form. */
  const regex_t *regex;

  /** True, if this expression matched anything in its lifetime. */
  bool has_matched;

  /** The next element, or NULL. */
  RegexList *next;
};

/** Represents a node in the search tree. */
typedef struct SearchNode SearchNode;
struct SearchNode
{
  /** The name or expression of the node. The buffer inside this string is
    guaranteed to be null-terminated. */
  String name;

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

  /** The next search node, or NULL. */
  SearchNode *next;
};

extern SearchNode *searchTreeLoad(const char *path);

#endif
