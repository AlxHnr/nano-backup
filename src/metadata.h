/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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

#ifndef _NANO_BACKUP_METADATA_H_
#define _NANO_BACKUP_METADATA_H_

#include <stdint.h>
#include <sys/types.h>

#include <openssl/sha.h>

#include "string-utils.h"
#include "string-table.h"
#include "backup-policies.h"

/** Stores the metadata of a regular file. */
typedef struct
{
  mode_t mode; /**< The permission bits of the file. */
  uint64_t size; /**< The file size. */

  /** The hash of the file. This array is only defined if the file size is
    greater than zero. If the files size is smaller than or equal to
    SHA_DIGEST_LENGTH, the entire file will be stored in the first bytes of
    this array. */
  uint8_t hash[SHA_DIGEST_LENGTH];

  /** The slot number of the corresponding file in the repository. It is
    used for generating unique filenames in case that two different files
    have the same size and hash. This variable is only defined if the file
    size is greater than SHA_DIGEST_LENGTH. */
  uint8_t slot;
}RegularMetadata;

/** The different states a filepath can represent at a specific backup. */
typedef enum
{
  PST_non_existing, /**< The path does not exist in the filesystem. */
  PST_regular,      /**< The path represents a regular file. */
  PST_symlink,      /**< The path represents a symbolic link. */
  PST_directory,    /**< The path represent a directory. */
}PathStateType;

/** Represents the state a path can have at a specific backup. */
typedef struct
{
  /** The type of the PathState. If the path state is PST_non_existing, all
    other values in this struct are undefined. */
  PathStateType type;

  uid_t uid; /**< The user id of the paths owner. */
  gid_t gid; /**< The group id of the paths owner. */
  time_t timestamp; /**< The paths last modification time. */

  /** Optional metadata, depending on the PathStateType. */
  union
  {
    RegularMetadata reg;    /**< The metadata of a regular file. */
    const char *sym_target; /**< The target path of a symlink. */
    mode_t dir_mode;        /**< The permission bits of a directory. */
  }metadata;
}PathState;

/** Represents a backup. A Backup is only valid, if its reference count
  is greater than zero. Otherwise its id and timestamp will be undefined.
*/
typedef struct Backup Backup;
struct Backup
{
  /** The id of the backup. It is only used as a helper variable for
    reading/writing metadata and may not be unique. */
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

/** A node representing a path in the filetree. */
typedef struct PathNode PathNode;
struct PathNode
{
  /** The full, absolute path inside the filesystem, containing a
    null-terminated buffer. */
  String path;

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
  /** The current backup. Its id will always be 0 and its timestamp will be
    undefined. This variable is shared across all newly created backup
    states. */
  Backup current_backup;

  /** The amount of elements in the backup history. */
  size_t backup_history_length;

  /** An array of backups. It will be NULL if its length is zero. */
  Backup *backup_history;

  /** The history of the repositories config file. Its path states will
    have always the type PST_regular and all its variables will be
    undefined, with the exception of metadata.reg. In metadata.reg the
    "mode" variable will be undefined. If this metadata doesn't have a
    config history, it will point to NULL. */
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

extern Metadata *newMetadata(void);
extern Metadata *loadMetadata(const char *path);
extern void writeMetadata(Metadata* metadata, const char *repo_path);

#endif
