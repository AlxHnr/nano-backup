#ifndef NANO_BACKUP_SRC_RESTORE_H
#define NANO_BACKUP_SRC_RESTORE_H

#include "metadata.h"
#include "str.h"

extern void initiateRestore(Metadata *metadata, size_t id, String path);
extern void restoreFile(String path, const RegularFileInfo *info,
                        String repo_path);
extern void finishRestore(const Metadata *metadata, size_t id,
                          String repo_path);

#endif
