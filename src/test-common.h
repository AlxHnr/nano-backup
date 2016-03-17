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
  Declares various functions to be used for testing data structures.
*/

#ifndef _NANO_BACKUP_TEST_COMMON_H_
#define _NANO_BACKUP_TEST_COMMON_H_

#include "metadata.h"
#include "string-utils.h"

extern String getCwd(void);

extern void checkMetadata(Metadata *metadata,
                          size_t config_history_length,
                          bool check_path_table);
extern void mustHaveConf(Metadata *metadata, Backup *backup,
                         uint64_t size, uint8_t *hash, uint8_t slot);

extern PathNode *findNode(PathNode *start_node, const char *path_str,
                          BackupPolicy policy, size_t history_length,
                          size_t subnode_count);
extern void mustHaveNonExisting(PathNode *node, Backup *backup);
extern void mustHaveRegular(PathNode *node, Backup *backup, uid_t uid,
                            gid_t gid, time_t timestamp, mode_t mode,
                            uint64_t size, uint8_t *hash, uint8_t slot);
extern void mustHaveSymlink(PathNode *node, Backup *backup, uid_t uid,
                            gid_t gid, time_t timestamp,
                            const char *sym_target);
extern void mustHaveDirectory(PathNode *node, Backup *backup, uid_t uid,
                              gid_t gid, time_t timestamp, mode_t mode);

#endif