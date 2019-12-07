/** @file
  Implements functions for allocating growable memory from regions.
*/

#include "alloc-growable.h"

#include <stdlib.h>

#include "address-sanitizer.h"
#include "error-handling.h"
#include "global-region.h"
#include "safe-math.h"
#include "static-assert.h"

/** A header containing metadata for resizable fat pointers. */
typedef struct
{
  /** A pointer for updating the pointer attached to the region. */
  void **attached_pointer;

  /** The capacity of the allocated memory. */
  size_t capacity;
}Header;

/** Frees the given resizable memory chunk attached to a region. */
static void freeAttachedPointer(void *ptr)
{
  void **attached_pointer = ptr;
  free(*attached_pointer);
}

/** Like CR_RegionAlloc(), but returns memory growable with
  CR_EnsureCapacity(). This function assures the same alignment guarantees
  as CR_RegionAlloc(). The returned memory will be released with the region
  and should not be freed by the caller.
*/
void *CR_RegionAllocGrowable(CR_Region *r, size_t size)
{
  if(size == 0)
  {
    CR_ExitFailure("unable to allocate 0 bytes");
  }

  CR_StaticAssert(sizeof(Header) % 8 == 0);
  const size_t chunk_size = CR_SafeAdd(sizeof(Header), size);

  void **attached_pointer = CR_RegionAlloc(r, sizeof *attached_pointer);

  Header *header = malloc(chunk_size);
  if(header == NULL)
  {
    CR_ExitFailure("failed to allocate %zu bytes", chunk_size);
  }

  *attached_pointer = header;
  header->attached_pointer = attached_pointer;
  header->capacity = size;

  CR_RegionAttach(r, freeAttachedPointer, attached_pointer);

  ASAN_POISON_MEMORY_REGION(header, sizeof(Header));
  return header + 1;
}

/** Reallocates the given memory if it has not enough space to store the
  requested size.

  @param ptr Memory allocated via CR_RegionAllocGrowable(). If ptr is NULL,
  memory will be allocated and bound to the lifetime of the entire program.
  @param size The amount of bytes that should fit into the given ptr.

  @return The possibly reallocated memory.
*/
void *CR_EnsureCapacity(void *ptr, size_t size)
{
  if(size == 0)
  {
    CR_ExitFailure("unable to allocate 0 bytes");
  }
  else if(ptr == NULL)
  {
    return CR_RegionAllocGrowable(CR_GetGlobalRegion(), size);
  }

  Header *header = (Header *)ptr - 1;
  ASAN_UNPOISON_MEMORY_REGION(header, sizeof(Header));
  if(size <= header->capacity)
  {
    ASAN_POISON_MEMORY_REGION(header, sizeof(Header));
    return ptr;
  }

  const size_t chunk_size = CR_SafeAdd(sizeof(Header), size);
  Header *reallocated_header = realloc(header, chunk_size);
  if(reallocated_header == NULL)
  {
    CR_ExitFailure("failed to reallocate %zu bytes", chunk_size);
  }

  *reallocated_header->attached_pointer = reallocated_header;
  reallocated_header->capacity = size;

  ASAN_POISON_MEMORY_REGION(reallocated_header, sizeof(Header));
  return reallocated_header + 1;
}
