/** @file
  Declares various helpers for working with repositories.
*/

#ifndef NANO_BACKUP_REPOSITORY_H
#define NANO_BACKUP_REPOSITORY_H

#include <stdbool.h>
#include <sys/types.h>

#include "buffer.h"
#include "file-hash.h"
#include "string-utils.h"

/** An opaque struct, which allows safe writing of files into backup
  repositories. */
typedef struct RepoWriter RepoWriter;

/** An opaque struct, which allows reading files from repositories. */
typedef struct RepoReader RepoReader;

/** Stores the metadata of a regular file. */
typedef struct
{
  mode_t mode; /**< The permission bits of the file. */
  time_t timestamp; /**< The files last modification time. */
  uint64_t size; /**< The file size. */

  /** The hash of the file. This array is only defined if the file size is
    greater than zero. If the files size is smaller than or equal to
    FILE_HASH_SIZE, the entire file will be stored in the first bytes of
    this array. */
  uint8_t hash[FILE_HASH_SIZE];

  /** The slot number of the corresponding file in the repository. It is
    used for generating unique filenames in case that two different files
    have the same size and hash. This variable is only defined if the file
    size is greater than FILE_HASH_SIZE. */
  uint8_t slot;
}RegularFileInfo;

extern bool repoRegularFileExists(String repo_path,
                                  const RegularFileInfo *info);
extern void repoBuildRegularFilePath(Buffer **buffer,
                                     const RegularFileInfo *info);

extern RepoReader *repoReaderOpenFile(const char *repo_path,
                                      const char *source_file_path,
                                      const RegularFileInfo *info);
extern void repoReaderRead(void *data, size_t size, RepoReader *reader);
extern void repoReaderClose(RepoReader *reader);

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
