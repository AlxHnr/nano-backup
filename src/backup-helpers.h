#ifndef NANO_BACKUP_SRC_BACKUP_HELPERS_H
#define NANO_BACKUP_SRC_BACKUP_HELPERS_H

#include <sys/stat.h>

#include "metadata.h"
#include "str.h"

extern void readSymlink(StringView path, struct stat stats,
                        char **buffer_ptr);

typedef struct
{
  Allocator *a;
  Allocator *reusable_buffer;
} AllocatorPair;

extern void applyNodeChanges(AllocatorPair *allocator_pair, PathNode *node,
                             PathState *state, struct stat stats);

#endif
