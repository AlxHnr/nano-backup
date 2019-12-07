/** @file
  Implements a struct for mapping strings to arbitrary data.
*/

#include "string-table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "memory-pool.h"
#include "safe-wrappers.h"

#include "SipHash/siphash.h"

/** A string table bucket. */
typedef struct Bucket Bucket;
struct Bucket
{
  /** The hash of the key. Required to resize the string table. */
  uint64_t hash;

  String key; /**< The key that was mapped. */
  void *data; /**< The data associated with the key. */

  Bucket *next; /**< The next bucked in the list, or NULL. */
};

struct StringTable
{
  Bucket **buckets; /**< An array of buckets. */
  size_t capacity; /**< The amount of buckets in the String table. */
  size_t associations; /**< The amount of associations in the table. */
  uint32_t secret_key[4]; /**< Secret key for the hash function. */

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
    while(bucket)
    {
      Bucket *bucket_to_move = bucket;
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

/** Initializes the secret_key in the given StringTable. */
static void initSecretKey(StringTable *table)
{
  static const size_t secret_key_length =
    sizeof(table->secret_key)/sizeof(table->secret_key[0]);

  for(size_t index = 0; index < secret_key_length; index++)
  {
    table->secret_key[index] = sRand();
  }
}

/** Creates a new, dynamically growing StringTable.

  @return A StringTable which must be freed by the caller using
  strTableFree().
*/
StringTable *strTableNew(void)
{
  StringTable *table = sMalloc(sizeof *table);
  table->alloc_function = sMalloc;
  table->associations = 0;
  table->capacity = 32;
  initSecretKey(table);

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
  which should not be freed by the caller. Passing it to strTableFree() is
  safe and will do nothing.
*/
StringTable *strTableNewFixed(size_t item_count)
{
  StringTable *table = mpAlloc(sizeof *table);
  table->capacity = sSizeMul(item_count, 2);
  table->alloc_function = mpAlloc;
  table->associations = 0;
  initSecretKey(table);

  size_t array_size = sSizeMul(table->capacity, sizeof *table->buckets);
  table->buckets = mpAlloc(array_size);

  /* Initialize all buckets to NULL. */
  memset(table->buckets, 0, array_size);

  return table;
}

/** Frees all memory associated with the given StringTable.

  @param table The StringTable that should be freed.
*/
void strTableFree(StringTable *table)
{
  if(table->alloc_function != sMalloc)
  {
    return;
  }

  for(size_t index = 0; index < table->capacity; index++)
  {
    Bucket *bucket = table->buckets[index];
    while(bucket)
    {
      Bucket *bucket_to_free = bucket;
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
void strTableMap(StringTable *table, String key, void *data)
{
  /* Try to resize hash table, if its capacity was reached. */
  if(table->associations == table->capacity)
  {
    doubleTableCapaticy(table);
  }

  /* Initialize bucket. */
  Bucket *bucket = table->alloc_function(sizeof *bucket);
  bucket->hash = siphash((const uint8_t *)key.str, key.length,
                         (const uint8_t *)table->secret_key);
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
void *strTableGet(StringTable *table, String key)
{
  const size_t hash = siphash((const uint8_t *)key.str, key.length,
                              (const uint8_t *)table->secret_key);
  const size_t bucket_id = hash % table->capacity;

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
