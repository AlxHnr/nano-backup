/** @file
  Declares functions for calculating the hash of a file.
*/

#ifndef NANO_BACKUP_FILE_HASH_H
#define NANO_BACKUP_FILE_HASH_H

#include <stdint.h>
#include <sys/stat.h>

#include <openssl/sha.h>

/** The amount of bytes required to store a files hash. */
#define FILE_HASH_SIZE SHA_DIGEST_LENGTH

extern void fileHash(const char *path, struct stat stats, uint8_t *hash);

#endif
