#include "allocator.h"

#include <stdlib.h>

#include "CRegion/alloc-growable.h"
#include "error-handling.h"

struct Allocator
{
  enum
  {
    ALT_always_failing,
    ALT_malloc,
    ALT_region,
    ALT_single_growable_buffer,
  } type;

  /** Optional pointers depending on `type`. */
  union
  {
    CR_Region *r;
    void *growable_buffer;
  } pointers;
};

/** Allocate memory using the given allocator or terminate the program with
  an error message.

  @param a Allocator to use.
  @param size Amount of bytes to allocate. 0 will be treated as an error.
*/
void *allocate(Allocator *a, const size_t size)
{
  if(size == 0)
  {
    die("unable to allocate 0 bytes");
  }

  void *data = NULL;
  switch(a->type)
  {
    case ALT_always_failing: /* Leave data pointing to NULL. */ break;
    case ALT_malloc: data = malloc(size); break;
    case ALT_region: data = CR_RegionAlloc(a->pointers.r, size); break;
    case ALT_single_growable_buffer:
      a->pointers.growable_buffer =
        CR_EnsureCapacity(a->pointers.growable_buffer, size);
      data = a->pointers.growable_buffer;
      break;
  }

  if(data == NULL)
  {
    die("out of memory: failed to allocate %zu bytes", size);
  }
  return data;
}

/** @return Static malloc wrapper which allocates memory that has to be
  released by using free(). */
Allocator *allocatorWrapMalloc(void)
{
  static Allocator a = { .type = ALT_malloc };
  return &a;
}

/** @return Allocator which lifetime is bound to the given region. */
Allocator *allocatorWrapRegion(CR_Region *r)
{
  Allocator *a = CR_RegionAlloc(r, sizeof *a);
  a->type = ALT_region;
  a->pointers.r = r;
  return a;
}

/** Create an allocator which always returns the same growable buffer. All
  memory allocated trough this allocator will be invalidated by further
  calls to allocate().

  @param r Region to which the lifetime of the growable buffer should be
  bound to.

  @return Allocator which lifetime is bound to the given region.
*/
Allocator *allocatorWrapOneSingleGrowableBuffer(CR_Region *r)
{
  Allocator *a = CR_RegionAlloc(r, sizeof *a);
  a->type = ALT_single_growable_buffer;
  a->pointers.growable_buffer = CR_RegionAllocGrowable(r, 1);
  return a;
}

/** @return Static allocator which always returns NULL for testing. */
Allocator *allocatorWrapAlwaysFailing(void)
{
  static Allocator a = { .type = ALT_always_failing };
  return &a;
}
