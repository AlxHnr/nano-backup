#ifndef NANO_BACKUP_TEST_METADATA_UTIL_H
#define NANO_BACKUP_TEST_METADATA_UTIL_H

#include "CRegion/region.h"
#include "metadata.h"

extern Metadata *createEmptyMetadata(CR_Region *r, size_t backup_history_length);
extern void initHistPoint(Metadata *metadata, size_t index, size_t id, time_t modification_time);
extern PathNode *createPathNode(const char *path_str, BackupPolicy policy, PathNode *parent_node,
                                Metadata *metadata);
void assignRegularValues(PathState *state, mode_t permission_bits, time_t modification_time, uint64_t size,
                         const uint8_t *hash, uint8_t slot);
extern void appendHist(CR_Region *r, PathNode *node, Backup *backup, PathState state);
extern void appendHistNonExisting(CR_Region *r, PathNode *node, Backup *backup);
extern void appendHistRegular(CR_Region *r, PathNode *node, Backup *backup, uid_t uid, gid_t gid,
                              time_t modification_time, mode_t permission_bits, uint64_t size, const uint8_t *hash,
                              uint8_t slot);
extern void appendHistSymlink(CR_Region *r, PathNode *node, Backup *backup, uid_t uid, gid_t gid,
                              const char *symlink_target);
extern void appendHistDirectory(CR_Region *r, PathNode *node, Backup *backup, uid_t uid, gid_t gid,
                                time_t modification_time, mode_t permission_bits);
extern void appendConfHist(Metadata *metadata, Backup *backup, uint64_t size, const uint8_t *hash, uint8_t slot);

#endif
