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
  @file search.h Declares functions for searching the filesystem.
*/

#ifndef _NANO_BACKUP_SEARCH_H_
#define _NANO_BACKUP_SEARCH_H_

#include <stdbool.h>

#include <sys/stat.h>

#include "search-tree.h"
#include "string-utils.h"
#include "backup-policies.h"

/** An opaque struct representing a search. */
typedef struct SearchContext SearchContext;

typedef enum
{
  SRT_regular,
  SRT_symlink,
  SRT_directory,
  SRT_other,

  SRT_end_of_directory,
  SRT_end_of_search,
}SearchResultType;

/**Contains the result of a search query. */
typedef struct
{
  /** The type of the result. */
  SearchResultType type;

  /** The path to the found file, relative to the root path of the
    associated SearchContext. The buffer of this variable shares memory
    with the SearchContext to which it belongs. It will be invalidated with
    the next call to searchGetNext(). */
  String path;

  /** The policy of the file. */
  BackupPolicy policy;

  /** Further informations about the file. */
  struct stat stats;
}SearchResult;

extern SearchContext *searchNew(String root, SearchNode *node);
extern SearchResult searchGetNext(SearchContext *context);

#endif
