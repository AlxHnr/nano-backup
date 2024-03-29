#ifndef NANO_BACKUP_SRC_SEARCH_RESULT_TYPE_H
#define NANO_BACKUP_SRC_SEARCH_RESULT_TYPE_H

typedef enum
{
  /** No search result. This type exists only for initializing SearchNodes
    that haven't matched anything. It will never be returned by a call to
    searchGetNext(). */
  SRT_none,

  SRT_regular_file = 1 << 0,
  SRT_symlink = 1 << 1,
  SRT_directory = 1 << 2,
  SRT_other = 1 << 3, /**< Any other filetype, like a block device. */

  /** The currently traversed directory has reached its end. In this case
    all values in a SearchResult are undefined. */
  SRT_end_of_directory = 1 << 4,

  /** The search has reached its end. All values in the SearchResult are
    undefined and the associated context was destroyed. This context should
    not be used anymore or it will lead to undefined behaviour. */
  SRT_end_of_search = 1 << 5,
} SearchResultType;

#endif
