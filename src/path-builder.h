/** @file
  Declares functions to build reusable paths based on Buffer.
*/

#ifndef NANO_BACKUP_SRC_PATH_BUILDER_H
#define NANO_BACKUP_SRC_PATH_BUILDER_H

#include "buffer.h"

extern size_t pathBuilderSet(Buffer **buffer, const char *path);
extern size_t pathBuilderAppend(Buffer **buffer, size_t length,
                                const char *path);

#endif
