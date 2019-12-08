/** @file
  Declares functions to build reusable paths based on Buffer.
*/

#ifndef NANO_BACKUP_SRC_PATH_BUILDER_H
#define NANO_BACKUP_SRC_PATH_BUILDER_H

#include <stddef.h>

extern size_t pathBuilderSet(char **buffer_ptr, const char *path);
extern size_t pathBuilderAppend(char **buffer_ptr, size_t length,
                                const char *path);

#endif
