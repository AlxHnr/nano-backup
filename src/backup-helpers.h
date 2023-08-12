#ifndef NANO_BACKUP_SRC_BACKUP_HELPERS_H
#define NANO_BACKUP_SRC_BACKUP_HELPERS_H

#include <sys/stat.h>

#include "metadata.h"
#include "str.h"

extern void readSymlink(String path, struct stat stats, char **buffer_ptr);
extern void applyNodeChanges(PathNode *node, PathState *state,
                             struct stat stats);

#endif
