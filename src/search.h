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
  Declares functions for searching the filesystem.
*/

#ifndef _NANO_BACKUP_SEARCH_H_
#define _NANO_BACKUP_SEARCH_H_

#include "search-tree.h"
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
