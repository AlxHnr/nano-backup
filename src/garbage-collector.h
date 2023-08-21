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

/** Callback for implementing progress animations based on rough estimates.
  Will be called for each visited item. If there are no items to visit,
  this function will never be called.

  @param max_call_limit Upper bound (constant) on how often this function
  may be called. This value is based on estimates on how many items exist
  for visiting.
*/
typedef void GCProgressCallback(size_t max_call_limit, void *user_data);

extern GCStatistics
collectGarbageProgress(const Metadata *metadata, StringView repo_path,
                       GCProgressCallback progress_callback,
                       void *callback_user_data);

#endif
