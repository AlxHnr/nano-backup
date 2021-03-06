/** @file
  Declares functions for searching the filesystem.
*/

#ifndef NANO_BACKUP_SRC_SEARCH_H
#define NANO_BACKUP_SRC_SEARCH_H

#include <sys/stat.h>

#include "search-tree.h"
#include "str.h"
#include "backup-policies.h"
#include "search-result-type.h"

/**Contains the result of a search query. */
typedef struct
{
  /** The type of the result. */
  SearchResultType type;

  /** The full path to the found file, containing a null-terminated buffer.
    It shares memory with the SearchContext to which it belongs and will be
    invalidated with the next call to searchGetNext(). */
  String path;

  /** The SearchNode which has matched the found path. Will be NULL if the
    path wasn't matched by any node. This node belongs to the search tree
    passed to searchNew(). */
  SearchNode *node;

  /** The policy of the file. */
  BackupPolicy policy;

  /** Further informations about the file. */
  struct stat stats;
}SearchResult;

/** An opaque struct representing a search. */
typedef struct SearchContext SearchContext;

extern SearchContext *searchNew(SearchNode *root_node);
extern SearchResult searchGetNext(SearchContext *context);

#endif
