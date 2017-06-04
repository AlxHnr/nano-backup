/** @file
  Various helper functions for doing backup related things.
*/

#ifndef NANO_BACKUP_BACKUP_HELPERS_H
#define NANO_BACKUP_BACKUP_HELPERS_H

#include <sys/stat.h>

#include "buffer.h"
#include "metadata.h"

extern void readSymlink(const char *path,
                        struct stat stats,
                        Buffer **buffer_ptr);
extern void applyNodeChanges(PathNode *node, PathState *state,
                             struct stat stats);

#endif
