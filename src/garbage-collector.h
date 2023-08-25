#ifndef NANO_BACKUP_SRC_GARBAGE_COLLECTOR_H
#define NANO_BACKUP_SRC_GARBAGE_COLLECTOR_H

#include "metadata.h"

typedef struct
{
  size_t deleted_items_count;
  uint64_t deleted_items_total_size;
} GCStatistics;

extern GCStatistics collectGarbage(const Metadata *metadata,
                                   StringView repo_path);

/** Callback for implementing progress animations. Will be called for each
  file spared from deletion. If there are no files to preserve, this
  function will never be called.

  @param deleted_items_size Volume in bytes of already deleted files. This
  value is imprecise and depends on the traversal order of the current
  filesystem. To get an accurate total, refer to `GCStatistics`.
  @param max_call_limit Upper bound (constant) on how often this function
  may be called. This value is based on estimates on how many files may
  exist inside the repository.
*/
typedef void GCProgressCallback(uint64_t deleted_items_size,
                                size_t max_call_limit, void *user_data);

extern GCStatistics
collectGarbageProgress(const Metadata *metadata, StringView repo_path,
                       GCProgressCallback progress_callback,
                       void *callback_user_data);

#endif
