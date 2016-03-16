/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

/** @file
  Declares functions for calculating the hash of a file.
*/

#ifndef _NANO_BACKUP_FILE_HASH_H_
#define _NANO_BACKUP_FILE_HASH_H_

#include <stdint.h>
#include <sys/stat.h>

#include <openssl/sha.h>

/** The amount of bytes required to store a files hash. */
#define FILE_HASH_SIZE SHA_DIGEST_LENGTH

extern void fileHash(const char *path, struct stat stats, uint8_t *hash);

#endif
