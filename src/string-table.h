/** @file
  Declares functions for mapping strings to arbitrary data.
*/

#ifndef NANO_BACKUP_SRC_STRING_TABLE_H
#define NANO_BACKUP_SRC_STRING_TABLE_H

#include <stddef.h>

#include "str.h"

/** An opaque struct, which allows mapping strings to arbitrary data and
  grows dynamically.
*/
typedef struct StringTable StringTable;

extern StringTable *strTableNew(void);
extern StringTable *strTableNewFixed(size_t item_count);
extern void strTableFree(StringTable *table);

extern void strTableMap(StringTable *table, String key, void *data);
extern void *strTableGet(StringTable *table, String key);

#endif
