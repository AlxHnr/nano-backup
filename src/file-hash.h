#ifndef NANO_BACKUP_SRC_FILE_HASH_H
#define NANO_BACKUP_SRC_FILE_HASH_H

#include <stdint.h>
#include <sys/stat.h>

#include "str.h"

/** The amount of bytes required to store a files hash. */
#define FILE_HASH_SIZE ((size_t)20)

extern void fileHash(String path, struct stat stats, uint8_t *hash);

#endif
