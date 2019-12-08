/** @file
  Implements a struct for mapping strings to arbitrary data.
*/

#include "string-table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "SipHash/siphash.h"

#include "safe-math.h"
#include "safe-wrappers.h"

/** Bucket for storing colliding keys. */
typedef struct Bucket Bucket;
struct Bucket
{
  /** Hash of the key. Required to resize the string table. */
  uint64_t hash;

  String key; /**< Mapped key. */
  void *data; /**< Data associated with the key. */

  Bucket *next; /**< Next bucked in the list or NULL. */
};

struct StringTable
{
  CR_Region *region; /**< Region used by this table for allocations. */
  Bucket **buckets; /**< An array of buckets. */
  size_t capacity; /**< The amount of buckets in the String table. */
  size_t associations; /**< The amount of associations in the table. */
  uint8_t secret_key[16]; /**< Secret key for SipHash. */
};

/** Double the capacity of the given table and move all buckets to their
  new destination.

  @param table Table to resize.
*/
static void doubleTableCapaticy(StringTable *table)
{
  const size_t new_capacity = sSizeMul(table->capacity, 2);
  const size_t new_array_size =
    sSizeMul(new_capacity, sizeof(*table->buckets));

  Bucket **new_buckets = sMalloc(new_array_size);
  memset(new_buckets, 0, new_array_size);

  /* Copy all associations into their new location. */
  for(size_t index = 0; index < table->capacity; index++)
  {
    Bucket *bucket = table->buckets[index];
    while(bucket)
    {
      Bucket *bucket_to_move = bucket;
      bucket = bucket->next;

      const size_t new_bucket_id = bucket_to_move->hash % new_capacity;
      bucket_to_move->next = new_buckets[new_bucket_id];
      new_buckets[new_bucket_id] = bucket_to_move;
    }
  }

  free(table->buckets);
  table->buckets = new_buckets;
  table->capacity = new_capacity;
}

/** Release callback to be attached to a region.

  @param data Pointer to the table which should be release.
*/
static void releaseStringTable(void *data)
{
  StringTable *table = data;
  free(table->buckets);
}

/** Creates a dynamically growing table for mapping strings to arbitrary
  data.

  @param region Region to use for allocations.

  @return Table which lifetime will be bound to the given region.
*/
StringTable *strTableNew(CR_Region *region)
{
  StringTable *table = CR_RegionAlloc(region, sizeof(*table));
  table->region = region;
  table->associations = 0;
  table->capacity = 32; /* A small initial value allows the test suite to
                           cover table resizing. */

  for(size_t index = 0; index < sizeof(table->secret_key); index++)
  {
    table->secret_key[index] = sRand() % UINT8_MAX;
  }

  const size_t array_size =
    sSizeMul(table->capacity, sizeof(*table->buckets));
  table->buckets = sMalloc(array_size);
  CR_RegionAttach(table->region, releaseStringTable, table);
  memset(table->buckets, 0, array_size);

  return table;
}

/** Associates the given key with the specified data. This function does
  not check whether the given key was already mapped. It will simply create
  another association with undefined order.

  @param table The table in which the association should be stored.
  @param key The key to which the given data should be mapped to. The table
  will keep a reference to this key, so the caller should not modify or
  free it, unless the given table is not used anymore.
  @param data The data that should be mapped to the key. The table will
  only store a reference to the data, so the caller should not move it
  unless the table is not used anymore.
*/
void strTableMap(StringTable *table, String key, void *data)
{
  if(table->associations == table->capacity)
  {
    doubleTableCapaticy(table);
  }

  Bucket *bucket = CR_RegionAlloc(table->region, sizeof(*bucket));
  bucket->hash =
    siphash((const uint8_t *)key.content, key.length, table->secret_key);
  strSet(&bucket->key, key);
  bucket->data = data;

  /* Prepend the bucket to the bucket slot in the bucket array. */
  const size_t bucket_id = bucket->hash % table->capacity;
  bucket->next = table->buckets[bucket_id];
  table->buckets[bucket_id] = bucket;

  table->associations++;
}

/** Returns the value associated with the given key.

  @param table Table that contains the association.
  @param key The key for which the value should be returned.

  @return Associated data or NULL if the key was not found.
*/
void *strTableGet(StringTable *table, String key)
{
  const size_t hash =
    siphash((const uint8_t *)key.content, key.length, table->secret_key);
  const size_t bucket_id = hash % table->capacity;

  for(Bucket *bucket = table->buckets[bucket_id];
      bucket != NULL; bucket = bucket->next)
  {
    if(strEqual(key, bucket->key))
    {
      return bucket->data;
    }
  }

  return NULL;
}
