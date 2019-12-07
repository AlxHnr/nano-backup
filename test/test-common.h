/** @file
  Declares various functions to be used for testing data structures.
*/

#ifndef NANO_BACKUP_TEST_TEST_COMMON_H
#define NANO_BACKUP_TEST_TEST_COMMON_H

#include "metadata.h"
#include "str.h"

extern String getCwd(void);
extern size_t countItemsInDir(const char *path);

extern void checkMetadata(Metadata *metadata,
                          size_t config_history_length,
                          bool check_path_table);
extern void checkHistPoint(Metadata *metadata, size_t index, size_t id,
                           time_t timestamp, size_t ref_count);
extern void mustHaveConf(Metadata *metadata, const Backup *backup,
                         uint64_t size, const uint8_t *hash, uint8_t slot);

extern PathNode *findPathNode(PathNode *start_node, const char *path_str,
                              BackupHint hint, BackupPolicy policy,
                              size_t history_length, size_t subnode_count);
extern void mustHaveNonExisting(PathNode *node, const Backup *backup);
extern void mustHaveRegular(PathNode *node, const Backup *backup,
                            uid_t uid, gid_t gid, time_t timestamp,
                            mode_t mode, uint64_t size,
                            const uint8_t *hash, uint8_t slot);
extern void mustHaveSymlink(PathNode *node, const Backup *backup,
                            uid_t uid, gid_t gid, const char *sym_target);
extern void mustHaveDirectory(PathNode *node, const Backup *backup,
                              uid_t uid, gid_t gid, time_t timestamp,
                              mode_t mode);

#endif
