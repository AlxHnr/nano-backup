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

typedef struct
{
  uid_t uid;
  gid_t gid;
  time_t timestamp;

  mode_t mode;
  size_t size;
  uint8_t hash[SHA_DIGEST_LENGTH];
}RegularMetadata;

typedef struct
{
  uid_t uid;
  gid_t gid;
  time_t timestamp;

  const char *target;
}SymlinkMetadata;

typedef struct
{
  uid_t uid;
  gid_t gid;
  time_t timestamp;

  mode_t mode;
}DirectoryMetadata;

typedef enum
{
  MNT_none,
  MNT_file,
  MNT_symlink,
  MNT_directory,
}FileStateType;

typedef struct
{
  FileStateType type;
  union
  {
    RegularMetadata reg;
    SymlinkMetadata sym;
    DirectoryMetadata dir;
  }metadata;
}FileState;

typedef struct Backup Backup;
struct Backup
{
  size_t id;
  time_t timestamp;
  size_t ref_count;
};

typedef struct FileHistory FileHistory;
struct FileHistory
{
  Backup *backup;
  FileState state;
  FileHistory *next;
};

typedef struct FileNode FileNode;
struct FileNode
{
  String path;
  BackupPolicy policy;
  FileHistory *history;

  FileNode *subnodes;
  FileNode *next;
};

typedef struct
{
  String repo_path;

  Backup current_backup;
  Backup *backup_history;
  size_t backup_history_length;

  FileHistory *config_history;

  FileNode *file_metadata;
  StringTable *file_paths;
}Metadata;

#endif
