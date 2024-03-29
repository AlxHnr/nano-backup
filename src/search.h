#ifndef NANO_BACKUP_SRC_SEARCH_H
#define NANO_BACKUP_SRC_SEARCH_H

#include <sys/stat.h>

#include "backup-policies.h"
#include "search-result-type.h"
#include "search-tree.h"
#include "str.h"

typedef struct
{
  SearchResultType type;

  /** The full path to the found file. It shares memory with the
    SearchIterator to which it belongs and will be invalidated with the
    next call to searchGetNext(). */
  StringView path;

  /** The SearchNode which has matched the found path. Will be NULL if the
    path wasn't matched by any node. This node belongs to the search tree
    passed to searchNew(). */
  const SearchNode *node;

  /** The policy of the file. */
  BackupPolicy policy;

  /** Further informations about the file. */
  struct stat stats;
} SearchResult;

typedef struct SearchIterator SearchIterator;

extern SearchIterator *searchNew(SearchNode *root_node);
extern SearchResult searchGetNext(SearchIterator *iterator);

#endif
