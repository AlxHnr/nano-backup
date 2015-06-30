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

/**
  @file string-table.h Declares functions for mapping strings to arbitrary
  data.
*/

#ifndef _NANO_BACKUP_STRING_TABLE_H_
#define _NANO_BACKUP_STRING_TABLE_H_

#include <stddef.h>

#include "string-utils.h"

/** An opaque struct, which allows mapping strings to arbitrary data and
  grows dynamically.
*/
typedef struct StringTable StringTable;

extern StringTable *strtableNew(size_t item_count);
extern void strtableFree(StringTable *table);

extern void strtableMap(String key, void *value);
extern void *strtableGet(String key);

#endif