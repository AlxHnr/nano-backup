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

/**
  @file search-tree.h Declares a struct that can be used for searching the
  file system.
*/

#ifndef _NANO_BACKUP_SEARCH_TREE_H_
#define _NANO_BACKUP_SEARCH_TREE_H_

#include <stdbool.h>

#include "string-matcher.h"
#include "backup-policies.h"

/** A list of StringMatcher. */
typedef struct StringMatcherList StringMatcherList;
struct StringMatcherList
{
  StringMatcher *matcher; /**< The string matcher. */
  StringMatcherList *next; /**< The next element, or NULL. */
};

/** Represents a node in the search tree. */
typedef struct SearchNode SearchNode;
struct SearchNode
{
  /** For checking whether a filename belongs to the this node or not. It
    is NULL, if this node is the root node of the tree. */
  StringMatcher *matcher;

  /** The backup policy for this node. */
  BackupPolicy policy;

  /** True, if the policy was inherited by the parent node. Otherwise this
    node got its own policy assigned inside the config file. */
  bool policy_inherited;

  /** A pointer to the first subnode, or NULL. */
  SearchNode *subnodes;

  /** True, if at least one subnode contains a regular expression.
    Subnodes of the subnode do not influence this value. */
  bool subnodes_contain_regex;

  /** Points to the search trees common ignore matcher list, which is
    shared across all of its nodes. This allows to search with every node.
    It matches filepaths that should be ignored. The common ignore matcher
    can point to NULL if its empty. */
  StringMatcherList **ignore_matcher_list;

  /** The next search node, or NULL. */
  SearchNode *next;
};

extern SearchNode *searchTreeLoad(const char *path);

#endif
