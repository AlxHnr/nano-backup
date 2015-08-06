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
  @file search-result.h Defines the result of a serach query.
*/

#ifndef _NANO_BACKUP_SEARCH_RESULT_H_
#define _NANO_BACKUP_SEARCH_RESULT_H_

#include <sys/stat.h>

#include "string-utils.h"
#include "backup-policies.h"

/** The type of a search result. */
typedef enum
{
  SRT_regular,   /**< A regular file. */
  SRT_symlink,   /**< A symbolic link. */
  SRT_directory, /**< A directory. */
  SRT_other,     /**< Any other filetype, like a block device. */

  /** The currently traversed directory has reached its end. In this case
    all values in a SearchResult are undefined. */
  SRT_end_of_directory,

  /** The search has reached its end. All values in the SearchResult are
    undefined and the associated context was destroyed. This context should
    not be used anymore or it will lead to undefined behaviour. */
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

#endif
