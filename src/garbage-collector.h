/** @file
  Defines functions for removing unreferenced files from the repository.
*/

#ifndef NANO_BACKUP_SRC_GARBAGE_COLLECTOR_H
#define NANO_BACKUP_SRC_GARBAGE_COLLECTOR_H

#include "metadata.h"

/** Contains statistics about collected files. */
typedef struct
{
  /** The amount of removed items from the repository. */
  size_t count;

  /** The size of removed files from the repository. */
  uint64_t size;
} GCStats;

extern GCStats collectGarbage(Metadata *metadata, String repo_path);

#endif
