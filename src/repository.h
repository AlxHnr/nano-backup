#ifndef NANO_BACKUP_SRC_REPOSITORY_H
#define NANO_BACKUP_SRC_REPOSITORY_H

#include <stdbool.h>
#include <sys/types.h>

#include "file-hash.h"
#include "str.h"

/** An opaque struct, which allows safe writing of files into backup
  repositories. */
typedef struct RepoWriter RepoWriter;

/** An opaque struct, which allows reading files from repositories. */
typedef struct RepoReader RepoReader;

typedef struct
{
  mode_t permission_bits;
  time_t modification_time;
  uint64_t size;

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
} RegularFileInfo;

extern bool repoRegularFileExists(StringView repo_path,
                                  const RegularFileInfo *info);
extern void repoBuildRegularFilePath(char **buffer_ptr,
                                     const RegularFileInfo *info);

extern RepoReader *repoReaderOpenFile(StringView repo_path,
                                      StringView source_file_path,
                                      const RegularFileInfo *info);
extern void repoReaderRead(void *data, size_t size, RepoReader *reader);
extern void repoReaderClose(RepoReader *reader);

extern RepoWriter *repoWriterOpenFile(StringView repo_path,
                                      StringView repo_tmp_file_path,
                                      StringView source_file_path,
                                      const RegularFileInfo *info);
extern RepoWriter *repoWriterOpenRaw(StringView repo_path,
                                     StringView repo_tmp_file_path,
                                     StringView source_file_path,
                                     StringView final_path);
extern void repoWriterWrite(const void *data, size_t size,
                            RepoWriter *writer);
extern void repoWriterClose(RepoWriter *writer);

extern void repoLockUntilExit(StringView repo_path);

#endif
