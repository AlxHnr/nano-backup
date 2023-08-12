#ifndef NANO_BACKUP_SRC_STRING_TABLE_H
#define NANO_BACKUP_SRC_STRING_TABLE_H

#include "CRegion/region.h"

#include "str.h"

/** Dynamically growing table for mapping strings to arbitrary data. */
typedef struct StringTable StringTable;

extern StringTable *strTableNew(CR_Region *region);
extern void strTableMap(StringTable *table, String key, void *data);
extern void *strTableGet(StringTable *table, String key);

#endif
