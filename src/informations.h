/*
  Copyright (c) 2016 Alexander Heinrich

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
  Declares functions to print informations and statistics to the user.
*/

#ifndef NANO_BACKUP_INFORMATIONS_H
#define NANO_BACKUP_INFORMATIONS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "metadata.h"
#include "search-tree.h"

/** Contains statistics about a specific change type. */
typedef struct
{
  /** The amount of items affected by this change. */
  size_t count;

  /** The size of all items affected by this change. */
  uint64_t size;
}ChangeStats;

/** Stores a shallow summary of the changes in a metadata tree. */
typedef struct
{
  ChangeStats new_items;     /**< Statistics about new items. */
  ChangeStats removed_items; /**< Statistics about removed items. */
  ChangeStats wiped_items;   /**< Statistics about wiped items. */
  ChangeStats changed_items; /**< Statistics about changed items. */
  bool other;                /**< Other changes exist. */

  /** At least one node in the current list affects the modification
    timestamp of the parent directory. */
  bool affects_parent_timestamp;
}MetadataChanges;

extern void printHumanReadableSize(uint64_t size);
extern void printSearchTreeInfos(SearchNode *root_node);
extern MetadataChanges printMetadataChanges(Metadata *metadata);

#endif
