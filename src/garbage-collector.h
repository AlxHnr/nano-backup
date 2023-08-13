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

#endif
