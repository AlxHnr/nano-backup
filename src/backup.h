/** @file
  Declares functions to perform fundamental backup operations.
*/

#ifndef NANO_BACKUP_SRC_BACKUP_H
#define NANO_BACKUP_SRC_BACKUP_H

#include "metadata.h"
#include "search-tree.h"
#include "str.h"

extern void initiateBackup(Metadata *metadata, SearchNode *root_node);
extern void finishBackup(Metadata *metadata, String repo_path,
                         String repo_tmp_file_path);

#endif
