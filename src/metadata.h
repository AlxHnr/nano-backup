#ifndef NANO_BACKUP_SRC_METADATA_H
#define NANO_BACKUP_SRC_METADATA_H

#include <stdint.h>
#include <sys/types.h>

#include "CRegion/region.h"
#include "backup-policies.h"
#include "repository.h"
#include "str.h"
#include "string-table.h"

/** The different states a filepath can represent at a specific backup. */
typedef enum
{
  PST_non_existing,
  PST_regular_file,
  PST_symlink,
  PST_directory,
} PathStateType;

typedef struct
{
  mode_t permission_bits;
  time_t modification_time;
} DirectoryInfo;

/** Represents the state a path can have at a specific backup. */
typedef struct
{
  /** The type of the PathState. If the path state is PST_non_existing, all
    other values in this struct are undefined. */
  PathStateType type;

  uid_t uid;
  gid_t gid;

  /** Optional metadata, depending on the PathStateType. */
  union
  {
    RegularFileInfo file_info;
    StringView symlink_target;
    DirectoryInfo directory_info;
  } metadata;
} PathState;

/** Represents a backup. A Backup is only valid, if its reference count
  is greater than zero. Otherwise its id and timestamp will be undefined.
*/
typedef struct Backup Backup;
struct Backup
{
  /** The id of the backup. It is only used as a helper variable for
    reading/writing metadata. */
  size_t id;

  time_t completion_time;

  /** The amount of states in history belonging to this backup. */
  size_t ref_count;
};

typedef struct PathHistory PathHistory;
struct PathHistory
{
  /** Points at the backup point to which this state in history belongs. */
  Backup *backup;
  PathState state;
  PathHistory *next;
};

/** Defines various hints to denote certain PathNode changes during a
  backup. */
typedef enum
{
  /** The path has no hint and was neither added, changed or removed. Must
    be 0 to allow combining other hints. */
  BH_none = 0,

  /** The path has not changed since the last backup. Excludes all other
    values. */
  BH_unchanged,

  /* The following values are mutually exclusive, unless stated
     otherwise. */

  /** The path was added. */
  BH_added,

  /** The path was removed from the users system. */
  BH_removed,

  /** The path is not part of the backup anymore and will be wiped. */
  BH_not_part_of_repository,

  BH_regular_to_symlink,   /**< The file was replaced by a symlink. */
  BH_regular_to_directory, /**< The file was replaced by a directory. */
  BH_symlink_to_regular,   /**< The symlink was replaced by a file. */
  BH_symlink_to_directory, /**< The symlink was replaced by a directory. */
  BH_directory_to_regular, /**< The directory was replaced by a file. */
  BH_directory_to_symlink, /**< The directory was replaced by a symlink. */

  /* Special hints for restoring files. */
  BH_other_to_regular,   /**< Restoring unsupported file to regular. */
  BH_other_to_symlink,   /**< Restoring unsupported file to symlink. */
  BH_other_to_directory, /**< Restoring unsupported file to directory. */

  /* The following values can be combined using the or operator. They
     can't be used together with the values defined above. */

  /** The owner of the path has changed. */
  BH_owner_changed = 1 << 4,

  /** The permission bits of a file/directory have changed. */
  BH_permissions_changed = 1 << 5,

  /** The modification time of a file/directory has changed. */
  BH_timestamp_changed = 1 << 6,

  /** The content of a file/symlink has changed. */
  BH_content_changed = 1 << 7,

  /** The hash of a RegularFileInfo was set while checking for changes.
    This can be used to save unneeded hash computations. */
  BH_fresh_hash = 1 << 8,

  /* The following values can be combined with all other values defined
     above, except with BH_unchanged. */

  /** The policy of a path has changed. */
  BH_policy_changed = 1 << 9,

  /** A policy change causes the path to lose its history. */
  BH_loses_history = 1 << 10,
} BackupHint;

/** Assigns a single hint to a variable while preventing to set mutually
  exclusive bits. */
#define backupHintSet(var, hint) \
  var = (hint <= BH_unchanged              ? hint \
           : hint <= BH_other_to_directory ? ((var & ~0x1FF) | hint) \
           : hint <= BH_fresh_hash         ? ((var & ~0xF) | hint) \
                                           : (var | hint))

/** Returns the value without policy bits. */
#define backupHintNoPol(val) ((val)&0x1FF)

typedef struct PathNode PathNode;
struct PathNode
{
  /** The full, absolute path inside the filesystem. */
  StringView path;

  /** Contains temporary informations about this node. They will not be
    written to disk and are only used during a single backup. */
  BackupHint hint;

  /** The backup policy of the current path. */
  BackupPolicy policy;

  /** The history of this path. Contains at least one history state and is
    not NULL. */
  PathHistory *history;

  /** The subnodes of this node. A path can change its type from a regular
    file to a symlink or directory and vice versa during its lifetime. To
    simplify the implementation, the subnodes are stored independently of
    the pathtype. Can be NULL if the path never was a directory. */
  PathNode *subnodes;

  PathNode *next;
};

/** Represents the metadata of a repository. */
typedef struct
{
  /** Owns this metadata object. */
  CR_Region *r;

  /** The current backup. Its id will always be 0 and its timestamp will
    contain the time when the backup has finished. This variable is shared
    across all newly created backup states. */
  Backup current_backup;

  /** The amount of elements in the backup history. */
  size_t backup_history_length;

  /** An array of backups. It will be NULL if its length is zero. */
  Backup *backup_history;

  /** The history of the repositories config file. Its path states will
    have always the type PST_regular and all its variables will be
    undefined, with the exception of metadata.file_info. In
    metadata.file_info only "size", "hash" and "slot" will be defined. If
    this metadata doesn't have a config history, it will point to NULL. */
  PathHistory *config_history;

  /** The amount of paths in the tree. It is only used as a helper variable
    for reading/writing metadata and may not be accurate. */
  size_t total_path_count;

  /** A StringTable associating a full, absolute filepath with its
    PathNode. This table contains only paths that exist in the metadata
    file. New files discovered during a backup will not be added to this
    table. */
  StringTable *path_table;

  /** A list of backed up files in the filesystem. Can be NULL if this
    metadata doesn't contain any filepaths. */
  PathNode *paths;
} Metadata;

extern Metadata *metadataNew(CR_Region *r);
extern Metadata *metadataLoad(CR_Region *r, StringView path);
extern void metadataWrite(Metadata *metadata, StringView repo_path,
                          StringView repo_tmp_file_path,
                          StringView repo_metadata_path);

#endif
