/** @file
  Implements functions for allocating from regions.
*/

#include "region.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "error-handling.h"
#include "safe-math.h"
#include "static-assert.h"

#define alignment sizeof(uint64_t)
#define first_chunk_size 1024

/** A list of callbacks. */
typedef struct CallbackList CallbackList;
struct CallbackList
{
  CR_ReleaseCallback *callback;
  void *data;

  CallbackList *next;
};

/** A list of allocated memory chunks. Each element is located at the
  beginning of the chunk. By freeing the element, the entire chunk will be
  freed. */
typedef struct ChunkList ChunkList;
struct ChunkList
{
  ChunkList *next;
};

typedef struct
{
  unsigned char *chunk; /**< Allocated bytes. */
  size_t bytes_used; /**< The amount of used bytes in the chunk. */
  size_t capacity; /**< The total capacity of the chunk. */
  size_t next_chunk_size; /**< The size of the next chunk. */
}Chunk;

/** A region for allocation. */
struct CR_Region
{
  Chunk aligned; /**< Chunk for aligned memory. */
  Chunk unaligned; /**< Chunk for unaligned memory. */

  /** A list of allocated chunks for freeing on release. */
  ChunkList *chunk_list;

  /** A list of callbacks to call on release. */
  CallbackList *callback_list;

  /** A callback which was not yet inserted into the regions callback_list.
    Allocating an element into the callback_list could fail and terminate
    the program. Thus we store the callback here to ensure it can be
    accessed in that case. See the implementation of CR_RegionAttach() for
    more details. */
  CR_ReleaseCallback *pending_callback;
  void *pending_callback_data;

  /** The previous and next regions. */
  CR_Region *prev, *next;
};

/** A list of all allocated regions. */
static CR_Region *region_list = NULL;

/** Releases all known regions. */
static void releaseAllRegions(void)
{
  while(region_list != NULL)
  {
    CR_RegionRelease(region_list);
  }
}

/** Setups various stuff like atexit() handler the first time this function
  is called. */
static void ensureRegionsAreInitialized(void)
{
  static bool initialized = false;
  if(initialized == true)
  {
    return;
  }

  if(atexit(releaseAllRegions) != 0)
  {
    CR_ExitFailure("failed to register function with atexit");
  }

  initialized = true;
}

/** Wrapper around malloc which handles returned NULL pointers. */
static void *checkedMalloc(size_t size)
{
  void *data = malloc(size);
  if(data == NULL)
  {
    CR_ExitFailure("failed to allocate %zu bytes", size);
  }

  return data;
}

/** Creates a new CR_Region that gets freed automatically on exit, or
  manually via CR_RegionRelease(). */
CR_Region *CR_RegionNew(void)
{
  ensureRegionsAreInitialized();
  CR_StaticAssert(alignment == 8);
  CR_StaticAssert(sizeof(CR_Region) % alignment == 0);
  CR_StaticAssert(sizeof(ChunkList) % alignment == 0);
  CR_StaticAssert(first_chunk_size/2 % alignment == 0);
  CR_StaticAssert(sizeof(ChunkList) + sizeof(CR_Region) < first_chunk_size/2);

  /* The region and its chunk-list are part of the first chunk. */
  ChunkList *element = checkedMalloc(first_chunk_size);
  CR_Region *r = (CR_Region *)(element + 1);

  r->aligned.chunk = (unsigned char *)element;
  r->aligned.bytes_used = (sizeof *element) + (sizeof *r);
  r->aligned.capacity = first_chunk_size/2;
  r->aligned.next_chunk_size = first_chunk_size * 2;

  r->unaligned.chunk = &r->aligned.chunk[first_chunk_size/2];
  r->unaligned.bytes_used = 0;
  r->unaligned.capacity = first_chunk_size/2;
  r->unaligned.next_chunk_size = first_chunk_size * 2;

  r->chunk_list = element;
  r->chunk_list->next = NULL;

  r->callback_list = NULL;
  r->pending_callback = NULL;
  r->pending_callback_data = NULL;

  /* Prepend region to region list. */
  r->prev = NULL;
  r->next = region_list;

  if(region_list != NULL)
  {
    region_list->prev = r;
  }
  region_list = r;

  return r;
}

/** Returns the requested amount of bytes from the given chunk.

  @param chunk The chunk to use.
  @param size The amount of bytes to pop.

  @return The requested bytes.
*/
static void *popBytesFromChunk(Chunk *chunk, size_t size)
{
  void *data = &chunk->chunk[chunk->bytes_used];
  chunk->bytes_used += size;
  return data;
}

/** Allocates the requested amount of bytes from the given chunk. If the
  chunk is to small, a new one will be created. */
static void *allocFromChunk(CR_Region *r, Chunk *chunk, size_t size)
{
  if(size == 0)
  {
    CR_ExitFailure("unable to allocate 0 bytes");
  }

  if(size <= chunk->capacity - chunk->bytes_used)
  {
    return popBytesFromChunk(chunk, size);
  }
  else if(size < chunk->next_chunk_size - sizeof(ChunkList))
  {
    ChunkList *element = checkedMalloc(chunk->next_chunk_size);

    chunk->chunk = (unsigned char *)element;
    chunk->bytes_used = sizeof *element;
    chunk->capacity = chunk->next_chunk_size;

    element->next = r->chunk_list;
    r->chunk_list = element;

    chunk->next_chunk_size = CR_SafeMultiply(chunk->next_chunk_size, 2);

    return popBytesFromChunk(chunk, size);
  }
  else
  {
    ChunkList *element = checkedMalloc(CR_SafeAdd(sizeof(ChunkList), size));

    element->next = r->chunk_list;
    r->chunk_list = element;

    return element + 1;
  }
}

/** Allocate from the given region. The requested amount of bytes will be
  rounded up to the next multiple of sizeof(uint64_t). This ensures that
  subsequent allocations are aligned. If the current chunk in the specified
  region is too small, a new one will be created.

  @param r Region from which should be allocated.
  @param size Amount of bytes to allocate.

  @return Allocated memory. Will never be NULL.
*/
static void *allocFromChunkWithPadding(CR_Region *r, size_t size)
{
  const size_t padding =
    (alignment - (size & (alignment - 1))) & (alignment - 1);

  return allocFromChunk(r, &r->aligned, CR_SafeAdd(size, padding));
}

#ifdef CREGION_ALWAYS_FRESH_MALLOC
/** Allocate memory using malloc() and attach its lifetime to the given
  region.

  @param r Region to which the lifetime of the returned memory should be
  bound.
  @param size Amount of bytes to allocate.

  @return Requested memory which should not be freed by the caller. Will
  never be NULL.
*/
static void *rawMallocWithRegion(CR_Region *r, size_t size)
{
  if(size == 0)
  {
    CR_ExitFailure("unable to allocate 0 bytes");
  }

  void *data = checkedMalloc(size);
  CR_RegionAttach(r, free, data);

  return data;
}
#endif

/** Allocates memory from the given region. The returned memory will be
  aligned to an 8 byte boundary. This equals the size of the largest
  official C99 data type uint64_t.

  @param r The region to use for the allocation.
  @param size The amount of bytes to allocate.

  @return A pointer to the allocated memory. This function will never
  return NULL. The returned memory will be uninitialized and should not be
  freed or reallocated by the caller. Its lifetime will be bound to the
  region.
*/
void *CR_RegionAlloc(CR_Region *r, size_t size)
{
#ifdef CREGION_ALWAYS_FRESH_MALLOC
  return rawMallocWithRegion(r, size);
#else
  return allocFromChunkWithPadding(r, size);
#endif
}

/** Like CR_RegionAlloc() but without aligning memory. */
void *CR_RegionAllocUnaligned(CR_Region *r, size_t size)
{
#ifdef CREGION_ALWAYS_FRESH_MALLOC
  return rawMallocWithRegion(r, size);
#else
  return allocFromChunk(r, &r->unaligned, size);
#endif
}

/** Ensures that the given callback gets called when the specified region
  will be released. Callbacks will be called in reversed order of
  registration. The last registered callback will be called first.

  @param r The region to which the callback should be attached.
  @param callback A function, which should never call exit().
  @param data A pointer to custom data which will be passed to the
  callback.
*/
void CR_RegionAttach(CR_Region *r, CR_ReleaseCallback *callback, void *data)
{
  /* Store the callback inside the region as the current pending
     callback. This is required in case the following allocation
     fails and terminates the program. */
  r->pending_callback = callback;
  r->pending_callback_data = data;

  CallbackList *element = allocFromChunkWithPadding(r, sizeof *element);

  r->pending_callback = NULL;
  r->pending_callback_data = NULL;

  /* Prepend the callback to the regions callback-list. */
  element->callback = callback;
  element->data = data;

  element->next = r->callback_list;
  r->callback_list = element;
}

/** Frees the given region and calls all attached callbacks. */
void CR_RegionRelease(CR_Region *r)
{
  /* Call all attached callbacks. */
  if(r->pending_callback != NULL)
  {
    r->pending_callback(r->pending_callback_data);
  }
  for(CallbackList *element = r->callback_list;
      element != NULL; element = element->next)
  {
    element->callback(element->data);
  }

  /* Detach the region from the region-list. */
  if(r->prev != NULL)
  {
    r->prev->next = r->next;
  }
  if(r->next != NULL)
  {
    r->next->prev = r->prev;
  }
  if(r == region_list)
  {
    region_list = region_list->next;
  }

  /* Free all chunks associated with the region. */
  ChunkList *element = r->chunk_list;
  while(element != NULL)
  {
    ChunkList *next = element->next;
    free(element);
    element = next;
  }
}
