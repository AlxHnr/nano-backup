#ifndef NANO_BACKUP_TEST_TEST_COMMON_H
#define NANO_BACKUP_TEST_TEST_COMMON_H

#include "metadata.h"
#include "str.h"

extern StringView getCwd(Allocator *a);
extern size_t countItemsInDir(const char *path);

extern void checkMetadata(const Metadata *metadata, size_t config_history_length, bool check_path_table);
extern void checkHistPoint(const Metadata *metadata, size_t index, size_t id, time_t completion_time,
                           size_t ref_count);
extern void mustHaveConf(const Metadata *metadata, const Backup *backup, uint64_t size, const uint8_t *hash,
                         uint8_t slot);

extern PathNode *findPathNode(PathNode *start_node, const char *path_str, BackupHint hint, BackupPolicy policy,
                              size_t history_length, size_t subnode_count);
extern void mustHaveNonExisting(const PathNode *node, const Backup *backup);
extern void mustHaveRegular(const PathNode *node, const Backup *backup, uid_t uid, gid_t gid,
                            time_t modification_time, mode_t permission_bits, uint64_t size, const uint8_t *hash,
                            uint8_t slot);
extern void mustHaveSymlink(const PathNode *node, const Backup *backup, uid_t uid, gid_t gid,
                            const char *symlink_target);
extern void mustHaveDirectory(const PathNode *node, const Backup *backup, uid_t uid, gid_t gid,
                              time_t modification_time, mode_t permission_bits);
extern const char *nullTerminate(StringView string);

#endif
