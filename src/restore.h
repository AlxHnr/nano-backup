#ifndef NANO_BACKUP_SRC_RESTORE_H
#define NANO_BACKUP_SRC_RESTORE_H

#include "metadata.h"
#include "str.h"

extern void initiateRestore(Metadata *metadata, size_t id,
                            StringView path);
extern void restoreFile(StringView path, const RegularFileInfo *info,
                        StringView repo_path);
extern void finishRestore(const Metadata *metadata, size_t id,
                          StringView repo_path);

#endif
