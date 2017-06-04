/** @file
  Declares functions for restoring files.
*/

#ifndef NANO_BACKUP_RESTORE_H
#define NANO_BACKUP_RESTORE_H

#include "metadata.h"

extern void initiateRestore(Metadata *metadata, size_t id,
                            const char *path);

extern void restoreFile(const char *path,
                        const RegularFileInfo *info,
                        const char *repo_path);
extern void finishRestore(Metadata *metadata, size_t id,
                          const char *repo_path);

#endif
