#ifndef NANO_BACKUP_SRC_INFORMATIONS_H
#define NANO_BACKUP_SRC_INFORMATIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "metadata.h"
#include "search-tree.h"

typedef struct
{
  size_t affected_items_count;
  uint64_t affected_items_size_total;
} ChangeStats;

typedef struct
{
  ChangeStats new_items;
  ChangeStats removed_items;
  ChangeStats lost_items;
  ChangeStats changed_items;
  /** Amount of timestamp attribute changes which where not caused by
    subnode changes. */
  size_t changed_attributes;
  bool other_changes_exist;

  /** At least one node in the current list affects the modification
    timestamp of the parent directory. */
  bool affects_parent_timestamp;
} ChangeSummary;

extern void printHumanReadableSize(uint64_t size);
extern void printSearchTreeInfos(const SearchNode *root_node);
extern ChangeSummary
printMetadataChanges(const Metadata *metadata,
                     RegexList *summarize_expressions);
extern bool containsChanges(const ChangeSummary *changes);
extern void warnNodeMatches(const SearchNode *node, String string);

#endif
