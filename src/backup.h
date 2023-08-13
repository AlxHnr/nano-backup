#ifndef NANO_BACKUP_SRC_BACKUP_H
#define NANO_BACKUP_SRC_BACKUP_H

#include "metadata.h"
#include "search-tree.h"
#include "str.h"

extern void initiateBackup(Metadata *metadata, SearchNode *root_node);
extern void finishBackup(Metadata *metadata, StringView repo_path,
                         StringView repo_tmp_file_path);

#endif
