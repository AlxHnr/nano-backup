/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

/** @file
  Defines a tree structure which represents the metadata of a repository.
*/

#ifndef NANO_BACKUP_METADATA_H
#define NANO_BACKUP_METADATA_H

#include <stdint.h>
#include <sys/types.h>

#include "repository.h"
#include "string-utils.h"
#include "string-table.h"
#include "backup-policies.h"

/** The different states a filepath can represent at a specific backup. */
typedef enum
{
  PST_non_existing, /**< The path does not exist in the filesystem. */
  PST_regular,      /**< The path represents a regular file. */
  PST_symlink,      /**< The path represents a symbolic link. */
  PST_directory,    /**< The path represent a directory. */
}PathStateType;

/** Stores the metadata of a directory. */
typedef struct
{
  mode_t mode; /**< The directories permission bits. */
  time_t timestamp; /**< The directories last modification time. */
}DirectoryInfo;

/** Represents the state a path can have at a specific backup. */
typedef struct
{
  /** The type of the PathState. If the path state is PST_non_existing, all
    other values in this struct are undefined. */
  PathStateType type;

  uid_t uid; /**< The user id of the paths owner. */
  gid_t gid; /**< The group id of the paths owner. */

  /** Optional metadata, depending on the PathStateType. */
  union
  {
    RegularFileInfo reg;    /**< The metadata of a regular file. */
    const char *sym_target; /**< The target path of a symlink. */
    DirectoryInfo dir;     /**< The permission bits of a directory. */
  }metadata;
}PathState;

/** Represents a backup. A Backup is only valid, if its reference count
  is greater than zero. Otherwise its id and timestamp will be undefined.
*/
typedef struct Backup Backup;
struct Backup
{
  /** The id of the backup. It is only used as a helper variable for
    reading/writing metadata. */
  size_t id;

  /** The time at which the backup was completed. */
  time_t timestamp;

  /** The amount of states in history belonging to this backup. */
  size_t ref_count;
};

/** The history of a filepath. */
typedef struct PathHistory PathHistory;
struct PathHistory
{
  /** Points at the backup point to which this state in history belongs. */
  Backup *backup;

  /** The state of the path during this backup. */
  PathState state;

  /** The next node in the paths history, or NULL. */
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
}BackupHint;

/** Assigns a single hint to a variable while preventing to set mutually
  exclusive bits. */
#define backupHintSet(var, hint) \
  var = (hint <= BH_unchanged? hint: \
         hint <= BH_directory_to_symlink? ((var & ~0x1FF) | hint): \
         hint <= BH_fresh_hash? ((var & ~0xF) | hint): \
         (var | hint))

/** Returns the value without policy bits. */
#define backupHintNoPol(val) ((val) & 0x1FF)

/** A node representing a path in the filetree. */
typedef struct PathNode PathNode;
struct PathNode
{
  /** The full, absolute path inside the filesystem, containing a
    null-terminated buffer. */
  String path;

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

  /** The next path in the list, or NULL. */
  PathNode *next;
};

/** Represents the metadata of a repository. */
typedef struct
{
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
    undefined, with the exception of metadata.reg. In metadata.reg only
    "size", "hash" and "slot" will be defined. If this metadata doesn't
    have a config history, it will point to NULL. */
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
}Metadata;

extern Metadata *metadataNew(void);
extern Metadata *metadataLoad(const char *path);
extern void metadataWrite(Metadata *metadata,
                          const char *repo_path,
                          const char *repo_tmp_file_path,
                          const char *repo_metadata_path);

#endif
