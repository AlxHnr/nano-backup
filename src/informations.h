/** @file
  Declares functions to print informations and statistics to the user.
*/

#ifndef NANO_BACKUP_SRC_INFORMATIONS_H
#define NANO_BACKUP_SRC_INFORMATIONS_H

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
  ChangeStats lost_items;    /**< Statistics about lost items. */
  ChangeStats changed_items; /**< Statistics about changed items. */
  bool other;                /**< Other changes exist. */

  /** At least one node in the current list affects the modification
    timestamp of the parent directory. */
  bool affects_parent_timestamp;
}MetadataChanges;

extern void printHumanReadableSize(uint64_t size);
extern void printSearchTreeInfos(SearchNode *root_node);
extern MetadataChanges printMetadataChanges(Metadata *metadata);
extern void warnNodeMatches(SearchNode *node, String string);

#endif
