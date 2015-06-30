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
  @file string-table.c Implements a struct for mapping strings to arbitrary
  data.
*/

#include "string-table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "safe-wrappers.h"

/** A string table bucket. */
typedef struct Bucket Bucket;
struct Bucket
{
  String key; /**< The key that was mapped. */
  uint32_t hash; /**< The hash of the key. */
  void *data; /**< The data associated with the key. */

  Bucket *next; /**< The next bucked in the list, or NULL. */
};

struct StringTable
{
  Bucket **buckets; /**< An array of buckets. */
  size_t capacity; /**< The amount of buckets in the String table. */
  size_t associations; /**< The amount of associations in the table. */
};

/** Creates a new StringTable.

  @param item_count An approximate count of items, which the StringTable
  should be able to hold at least. If the StringTable is full, it will
  resize automatically. If unsure, pass 0 as argument.

  @return A new StringTable, which must be freed by the caller using
  strtableFree().
*/
StringTable *strtableNew(size_t item_count)
{
  StringTable *table = sMalloc(sizeof *table);
  table->capacity = item_count < 16 ? 32 : sSizeMul(item_count, 2);

  size_t array_size = sSizeMul(table->capacity, sizeof *table->buckets);
  table->buckets = sMalloc(array_size);

  /* Initialize all buckets to NULL. */
  memset(table->buckets, 0, array_size);

  return table;
}

/** Frees all memory associated with the given StringTable.

  @param table The StringTable that should be freed.
*/
void strtableFree(StringTable *table)
{
  for(size_t index = 0; index < table->capacity; index++)
  {
    Bucket *bucket = table->buckets[index];
    Bucket *bucket_to_free;
    while(bucket)
    {
      bucket_to_free = bucket;
      bucket = bucket->next;
      free(bucket_to_free);
    }
  }

  free(table->buckets);
  free(table);
}