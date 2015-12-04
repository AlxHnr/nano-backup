/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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
  Declares various helpers for working with repositories.
*/

#ifndef _NANO_BACKUP_REPOSITORY_H_
#define _NANO_BACKUP_REPOSITORY_H_

#include <stdbool.h>
#include <sys/types.h>

#include <openssl/sha.h>

#include "string-utils.h"

/** An opaque struct, which allows safe writing of files into backup
  repositories. */
typedef struct RepoWriter RepoWriter;

/** Stores the metadata of a regular file. */
typedef struct
{
  mode_t mode; /**< The permission bits of the file. */
  uint64_t size; /**< The file size. */

  /** The hash of the file. This array is only defined if the file size is
    greater than zero. If the files size is smaller than or equal to
    SHA_DIGEST_LENGTH, the entire file will be stored in the first bytes of
    this array. */
  uint8_t hash[SHA_DIGEST_LENGTH];

  /** The slot number of the corresponding file in the repository. It is
    used for generating unique filenames in case that two different files
    have the same size and hash. This variable is only defined if the file
    size is greater than SHA_DIGEST_LENGTH. */
  uint8_t slot;
}RegularFileInfo;

extern bool repoRegularFileExists(String repo_path,
                                  const RegularFileInfo *info);
extern RepoWriter *repoWriterOpenFile(const char *repo_path,
                                      const char *repo_tmp_file_path,
                                      const char *source_file_path,
                                      const RegularFileInfo *info);
extern RepoWriter *repoWriterOpenRaw(const char *repo_path,
                                     const char *repo_tmp_file_path,
                                     const char *source_file_path,
                                     const char *final_path);
extern void repoWriterWrite(const void *data, size_t size,
                            RepoWriter *writer);
extern void repoWriterClose(RepoWriter *writer);

#endif