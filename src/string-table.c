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

/** @file
  Implements a struct for mapping strings to arbitrary data.
*/

#include "string-table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "memory-pool.h"
#include "safe-wrappers.h"

/** A string table bucket. */
typedef struct Bucket Bucket;
struct Bucket
{
  /** The hash of the key. Required to resize the string table. */
  uint32_t hash;

  String key; /**< The key that was mapped. */
  void *data; /**< The data associated with the key. */

  Bucket *next; /**< The next bucked in the list, or NULL. */
};

struct StringTable
{
  Bucket **buckets; /**< An array of buckets. */
  size_t capacity; /**< The amount of buckets in the String table. */
  size_t associations; /**< The amount of associations in the table. */

  /** A pointer to the function used for allocating the table and its
    buckets. */
  void *(*alloc_function)(size_t);
};

/** Doubles the capacity of the given StringTable and moves all buckets to
  their new destination. If the given table is a fixed size table, this
  function will do nothing.

  @param table The table that should be resized.
*/
static void doubleTableCapaticy(StringTable *table)
{
  if(table->alloc_function != sMalloc)
  {
    return;
  }

  size_t new_capacity = sSizeMul(table->capacity, 2);
  size_t new_array_size = sSizeMul(new_capacity, sizeof *table->buckets);

  Bucket **new_buckets = sMalloc(new_array_size);

  /* Initialize all new buckets to NULL. */
  memset(new_buckets, 0, new_array_size);

  /* Copy all associations into their new location. */
  for(size_t index = 0; index < table->capacity; index++)
  {
    Bucket *bucket = table->buckets[index];
    Bucket *bucket_to_move;
    while(bucket)
    {
      bucket_to_move = bucket;
      bucket = bucket->next;

      size_t new_bucket_id = bucket_to_move->hash % new_capacity;
      bucket_to_move->next = new_buckets[new_bucket_id];
      new_buckets[new_bucket_id] = bucket_to_move;
    }
  }

  free(table->buckets);
  table->buckets = new_buckets;
  table->capacity = new_capacity;
}

/** Creates a new, dynamically growing StringTable.

  @return A StringTable which must be freed by the caller using
  strtableFree().
*/
StringTable *strtableNew(void)
{
  StringTable *table = sMalloc(sizeof *table);
  table->alloc_function = sMalloc;
  table->associations = 0;
  table->capacity = 32;

  size_t array_size = sSizeMul(table->capacity, sizeof *table->buckets);
  table->buckets = sMalloc(array_size);

  /* Initialize all buckets to NULL. */
  memset(table->buckets, 0, array_size);

  return table;
}

/** Creates a fixed size StringTable allocated inside the internal memory
  pool.

  @param item_count The amount of associations that the StringTable must be
  able to hold. Must be greater than 0, or the program will be terminated
  with an error.

  @return A new StringTable allocated inside the internal memory pool
  which should not be freed by the caller. Passing it to strtableFree() is
  safe and will do nothing.
*/
StringTable *strtableNewFixed(size_t item_count)
{
  StringTable *table = mpAlloc(sizeof *table);
  table->capacity = sSizeMul(item_count, 2);
  table->alloc_function = mpAlloc;
  table->associations = 0;

  size_t array_size = sSizeMul(table->capacity, sizeof *table->buckets);
  table->buckets = mpAlloc(array_size);

  /* Initialize all buckets to NULL. */
  memset(table->buckets, 0, array_size);

  return table;
}

/** Frees all memory associated with the given StringTable.

  @param table The StringTable that should be freed.
*/
void strtableFree(StringTable *table)
{
  if(table->alloc_function != sMalloc)
  {
    return;
  }

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

/** Associates the given key with the specified data. This function does
  not check whether the given key was already mapped. It will simply create
  another association with undefined order.

  @param table The table in which the association should be stored.
  @param key The key to which the given data should be mapped to. The table
  will keep a reference to this key, so the caller should not modify or
  free it, unless the given StringTable is not used anymore.
  @param data The data that should be mapped to the key. The StringTable
  will only store a reference to the data, so the caller should not move
  it unless the table is not used anymore.
*/
void strtableMap(StringTable *table, String key, void *data)
{
  /* Try to resize hash table, if its capacity was reached. */
  if(table->associations == table->capacity)
  {
    doubleTableCapaticy(table);
  }

  /* Initialize bucket. */
  Bucket *bucket = table->alloc_function(sizeof *bucket);
  bucket->hash = strHash(key);
  bucket->data = data;

  /* Copy the given key into a String with const members. */
  memcpy(&bucket->key, &key, sizeof(key));

  /* Prepend the bucket to the bucket slot in the bucket array. */
  size_t bucket_id = bucket->hash % table->capacity;
  bucket->next = table->buckets[bucket_id];
  table->buckets[bucket_id] = bucket;

  table->associations = sSizeAdd(table->associations, 1);
}

/** Returns the value associated with the given key.

  @param table The StringTable that contains the association.
  @param key The key for which the value should be returned.

  @return The associated data, or NULL if the key was not found.
*/
void *strtableGet(StringTable *table, String key)
{
  size_t bucket_id = strHash(key) % table->capacity;

  for(Bucket *bucket = table->buckets[bucket_id];
      bucket != NULL; bucket = bucket->next)
  {
    if(strCompare(key, bucket->key))
    {
      return bucket->data;
    }
  }

  return NULL;
}
