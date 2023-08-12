#ifndef NANO_BACKUP_SRC_GARBAGE_COLLECTOR_H
#define NANO_BACKUP_SRC_GARBAGE_COLLECTOR_H

#include "metadata.h"

typedef struct
{
  /** The amount of removed items from the repository. */
  size_t count;

  /** The size of removed files from the repository. */
  uint64_t size;
} GCStats;

extern GCStats collectGarbage(const Metadata *metadata, String repo_path);

#endif
