/** @file
  Declares functions to perform fundamental backup operations.
*/

#ifndef NANO_BACKUP_BACKUP_H
#define NANO_BACKUP_BACKUP_H

#include "metadata.h"
#include "search-tree.h"

extern void initiateBackup(Metadata *metadata, SearchNode *root_node);
extern void finishBackup(Metadata *metadata, const char *repo_path,
                         const char *repo_tmp_file_path);

#endif
