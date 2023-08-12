#ifndef NANO_BACKUP_SRC_INFORMATIONS_H
#define NANO_BACKUP_SRC_INFORMATIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "metadata.h"
#include "search-tree.h"

typedef struct
{
  /** The amount of items affected by this change. */
  size_t count;

  /** The size of all items affected by this change. */
  uint64_t size;
} ChangeStats;

/** Stores a shallow summary of the changes in a metadata tree. */
typedef struct
{
  ChangeStats new_items;
  ChangeStats removed_items;
  ChangeStats lost_items;
  ChangeStats changed_items;
  /** Amount of timestamp attribute changes which where not caused by
    subnode changes. */
  size_t changed_attributes;
  bool other; /**< Other changes exist. */

  /** At least one node in the current list affects the modification
    timestamp of the parent directory. */
  bool affects_parent_timestamp;
} MetadataChanges;

extern void printHumanReadableSize(uint64_t size);
extern void printSearchTreeInfos(const SearchNode *root_node);
extern MetadataChanges
printMetadataChanges(const Metadata *metadata,
                     RegexList *summarize_expressions);
extern bool containsChanges(const MetadataChanges *changes);
extern void warnNodeMatches(const SearchNode *node, String string);

#endif
