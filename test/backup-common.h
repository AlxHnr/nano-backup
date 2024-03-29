#ifndef NANO_BACKUP_TEST_BACKUP_COMMON_H
#define NANO_BACKUP_TEST_BACKUP_COMMON_H

#include "metadata.h"

extern PathNode *findCwdNode(Metadata *metadata, StringView cwd, BackupHint hint);
extern PathNode *findSubnode(PathNode *node, const char *subnode_name, BackupHint hint, BackupPolicy policy,
                             size_t requested_history_length, size_t requested_subnode_count);

extern time_t getParentTime(const char *path);
extern void restoreParentTime(const char *path, time_t time);

extern void makeDir(const char *path);
extern void makeSymlink(const char *target, const char *linkpath);
extern void generateFile(const char *path, const char *content, size_t repetitions);
extern void generateCollidingFiles(const uint8_t *hash, size_t size, size_t files_to_create);

extern void removePath(const char *path);
extern void regenerateFile(PathNode *node, const char *content, size_t repetitions);
extern void remakeSymlink(const char *new_target, const char *linkpath);

extern void assertTmpIsCleared(void);
extern PathHistory *findExistingHistPoint(PathNode *node);

extern void restoreRegularFile(const char *path, const RegularFileInfo *info);
extern void restoreWithTimeRecursively(PathNode *node);

extern void setStatCache(size_t index);
extern struct stat cachedStat(StringView path, struct stat (*stat_fun)(StringView));
extern void resetStatCache(void);

extern void mustHaveRegularStats(const PathNode *node, const Backup *backup, struct stat stats, uint64_t size,
                                 const uint8_t *hash, uint8_t slot);
extern void mustHaveRegularStat(const PathNode *node, const Backup *backup, uint64_t size, const uint8_t *hash,
                                uint8_t slot);
extern void mustHaveRegularCached(const PathNode *node, const Backup *backup, uint64_t size, const uint8_t *hash,
                                  uint8_t slot);

extern void mustHaveSymlinkStats(const PathNode *node, const Backup *backup, struct stat stats,
                                 const char *symlink_target);
extern void mustHaveSymlinkLStat(const PathNode *node, const Backup *backup, const char *symlink_target);
extern void mustHaveSymlinkLCached(const PathNode *node, const Backup *backup, const char *symlink_target);

extern void mustHaveDirectoryStats(const PathNode *node, const Backup *backup, struct stat stats);
extern void mustHaveDirectoryStat(const PathNode *node, const Backup *backup);
extern void mustHaveDirectoryCached(const PathNode *node, const Backup *backup);

extern PathNode *findFilesNode(Metadata *metadata, BackupHint hint, size_t subnode_count);
extern size_t cwd_depth(void);
extern void completeBackup(Metadata *metadata);
extern time_t phase_timestamps(size_t index);
extern size_t backup_counter(void);

extern void initBackupCommon(size_t stat_cache_count);

#endif
