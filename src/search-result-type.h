/*
  Copyright (c) 2016 Alexander Heinrich

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
  Defines the result of a search query.
*/

#ifndef NANO_BACKUP_SEARCH_RESULT_TYPE_H
#define NANO_BACKUP_SEARCH_RESULT_TYPE_H

/** The type of a search result. */
typedef enum
{
  /** No search result. This type exists only for initializing SearchNodes
    that haven't matched anything. It will never be returned by a call to
    searchGetNext(). */
  SRT_none,

  SRT_regular   = 1 << 0, /**< A regular file. */
  SRT_symlink   = 1 << 1, /**< A symbolic link. */
  SRT_directory = 1 << 2, /**< A directory. */
  SRT_other     = 1 << 3, /**< Any other filetype, like a block device. */

  /** The currently traversed directory has reached its end. In this case
    all values in a SearchResult are undefined. */
  SRT_end_of_directory = 1 << 4,

  /** The search has reached its end. All values in the SearchResult are
    undefined and the associated context was destroyed. This context should
    not be used anymore or it will lead to undefined behaviour. */
  SRT_end_of_search = 1 << 5,
}SearchResultType;

#endif
