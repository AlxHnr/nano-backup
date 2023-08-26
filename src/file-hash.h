#ifndef NANO_BACKUP_SRC_FILE_HASH_H
#define NANO_BACKUP_SRC_FILE_HASH_H

#include <stdint.h>
#include <sys/stat.h>

#include "str.h"

/** The amount of bytes required to store a files hash. */
#define FILE_HASH_SIZE ((size_t)20)

typedef void HashProgressCallback(size_t processed_block_size,
                                  void *user_data);

extern void fileHash(StringView filepath, struct stat stats,
                     uint8_t *hash_out,
                     HashProgressCallback progress_callback,
                     void *callback_user_data);

#endif
