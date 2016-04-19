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
  Declares functions to print informations and statistics to the user.
*/

#ifndef NANO_BACKUP_INFORMATIONS_H
#define NANO_BACKUP_INFORMATIONS_H

#include <stddef.h>
#include <stdint.h>

#include "metadata.h"
#include "search-tree.h"

/** Stores a shallow summary of the changes in a metadata tree. */
typedef struct
{
  /** The amount of new items added to the metadata tree. */
  size_t new_items_count;

  /** The total size of all new files added to the metadata tree. */
  uint64_t new_files_size;
}MetadataChanges;

extern void printHumanReadableSize(uint64_t size);
extern void printSearchTreeInfos(SearchNode *root_node);
extern MetadataChanges printMetadataChanges(Metadata *metadata);

#endif
