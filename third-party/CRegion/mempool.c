/** @file
  Implements a memory pool for reusing memory allocated from regions.
*/

#include "mempool.h"

#include <stdbool.h>

#include "address-sanitizer.h"
#include "error-handling.h"
#include "safe-math.h"
#include "static-assert.h"

/** Contains the state of a destructor. */
typedef enum
{
  DS_disabled, /**< The destructor is disabled. */
  DS_enabled,  /**< The destructor is enabled. */
  DS_already_called, /**< The destructor was already called. */
}DestructorState;

/** A header containing metadata for reusable fat pointers. */
typedef struct Header Header;
struct Header
{
  /** Contains the current destructor state. */
  DestructorState destructor_state;

  /** The memory pool to which this header belongs. */
  CR_Mempool *mp;

  /** The previous and next headers in the current list. */
  Header *prev, *next;
};

/** A memory pool for reusing allocated memory. */
struct CR_Mempool
{
  /** The region to which this mempool belongs. */
  CR_Region *r;

  /** The function to which the object will be passed on explicit calls to
    CR_DestroyObject(). */
  CR_FailableDestructor *explicit_destructor;

  /** The function to which the object will be passed if the pool gets
    released. */
  CR_ReleaseCallback *implicit_destructor;

  /** The size of an object + header. */
  size_t chunk_size;

  /** A list of all allocated chunks. Required for releasing them. */
  Header *allocated_chunks;

  /** A list of explicitly destroyed chunks ready for reuse. */
  Header *released_chunks;
};

#ifdef CREGION_ALWAYS_FRESH_MALLOC
#include <stdlib.h>

/** Free all chunks allocated by the given mempool. */
static void FREE_ALL_CHUNKS_IF_REQUIRED(CR_Mempool *mp)
{
  Header *header = mp->allocated_chunks;
  while(header != NULL)
  {
    ASAN_UNPOISON_MEMORY_REGION(header, sizeof(Header));
    Header *next = header->next;
    free(header);
    header = next;
  }
}
#else
#define FREE_ALL_CHUNKS_IF_REQUIRED(mp) ((void)(mp))
#endif

/** Destroys all objects in the given mempool using the implicit
  destructor. */
static void destroyObjects(void *data)
{
  CR_Mempool *mp = data;
  if(mp->implicit_destructor == NULL)
  {
    FREE_ALL_CHUNKS_IF_REQUIRED(mp);
    return;
  }

  for(Header *header = mp->allocated_chunks;
      header != NULL; header = header->next)
  {
    ASAN_UNPOISON_MEMORY_REGION(header, sizeof(Header));
    if(header->destructor_state == DS_enabled)
    {
      ASAN_POISON_MEMORY_REGION(header, sizeof(Header));
      mp->implicit_destructor(header + 1);
      ASAN_UNPOISON_MEMORY_REGION(header, sizeof(Header));
    }
  }

  FREE_ALL_CHUNKS_IF_REQUIRED(mp);
}

/** Creates a new mempool.

  @param r The region to which the mempool should be bound.
  @param object_size The size of each object which should be allocated by
  the mempool.
  @param explicit_destructor A function to which objects will be passed by
  CR_DestroyObject(). This function is allowed to fail and must be
  activated via CR_EnableObjectDestructor(). If this value is NULL, it will
  be ignored.
  @param implicit_destructor A function to which objects will be passed
  when the pool gets released. This function should not fail and must be
  activated via CR_EnableObjectDestructor(). If this value is NULL, it will
  be ignored.

  @return A memory pool from which objects can be allocated. The returned
  pool will be bound to the lifetime of the given region.
*/
CR_Mempool *CR_MempoolNew(CR_Region *r, size_t object_size,
                          CR_FailableDestructor *explicit_destructor,
                          CR_ReleaseCallback *implicit_destructor)
{
  if(object_size == 0)
  {
    CR_ExitFailure("unable to create memory pool for allocating zero size objects");
  }
  CR_StaticAssert(sizeof(Header) % 8 == 0);

  CR_Mempool *mp = CR_RegionAlloc(r, sizeof *mp);
  mp->r = r;
  mp->explicit_destructor = explicit_destructor;
  mp->implicit_destructor = implicit_destructor;
  mp->chunk_size = CR_SafeAdd(sizeof(Header), object_size);
  mp->allocated_chunks = NULL;
  mp->released_chunks = NULL;
  CR_RegionAttach(r, destroyObjects, mp);

  return mp;
}

/** Returns the next reusable chunk. If no chunk exists, a new one will be
  allocated and returned.

  @param mp The mempool to use.

  @return An uninitialized chunk.
*/
static void *getAvailableChunk(CR_Mempool *mp)
{
#ifdef CREGION_ALWAYS_FRESH_MALLOC
  void *data = malloc(mp->chunk_size);
  if(data == NULL)
  {
    CR_ExitFailure("failed to allocate %zu bytes", mp->chunk_size);
  }

  return data;
#else
  if(mp->released_chunks == NULL)
  {
    return CR_RegionAlloc(mp->r, mp->chunk_size);
  }
  else
  {
    void *chunk = mp->released_chunks;
    ASAN_UNPOISON_MEMORY_REGION(mp->released_chunks, mp->chunk_size);

    mp->released_chunks = mp->released_chunks->next;
    if(mp->released_chunks != NULL)
    {
      ASAN_UNPOISON_MEMORY_REGION(mp->released_chunks, sizeof(Header));
      mp->released_chunks->prev = NULL;
      ASAN_POISON_MEMORY_REGION(mp->released_chunks, sizeof(Header));
    }

    return chunk;
  }
#endif
}

/** Allocates from the given mempool.

  @param mp The mempool to allocate from.

  @return Uninitialized, reused memory.
*/
void *CR_MempoolAlloc(CR_Mempool *mp)
{
  Header *header = getAvailableChunk(mp);

  header->destructor_state = DS_disabled;
  header->mp = mp;
  header->prev = NULL;

  /* Prepend header to the mempools allocated chunk list. */
  header->next = mp->allocated_chunks;
  mp->allocated_chunks = header;

  if(header->next != NULL)
  {
    ASAN_UNPOISON_MEMORY_REGION(header->next, sizeof(Header));
    header->next->prev = header;
    ASAN_POISON_MEMORY_REGION(header->next, sizeof(Header));
  }

  ASAN_POISON_MEMORY_REGION(header, sizeof(Header));
  return header + 1;
}

/** Enables the destructor of the given object. This is used to signalize
  that an object is fully initialized.

  @param ptr An object created by CR_MempoolAlloc().
*/
void CR_EnableObjectDestructor(void *ptr)
{
  Header *header = (Header *)ptr - 1;
  ASAN_UNPOISON_MEMORY_REGION(header, sizeof(Header));
  header->destructor_state = DS_enabled;
  ASAN_POISON_MEMORY_REGION(header, sizeof(Header));
}

/** Destroys the given object and calls the explicit destructor, if enabled
  with CR_EnableObjectDestructor().

  @param ptr An object created by CR_MempoolAlloc().
*/
void CR_DestroyObject(void *ptr)
{
  Header *header = (Header *)ptr - 1;
  ASAN_UNPOISON_MEMORY_REGION(header, sizeof(Header));
  CR_Mempool *mp = header->mp;

  if(header->destructor_state == DS_already_called)
  {
    CR_ExitFailure("passed the same object to CR_DestroyObject() twice");
  }
  const bool destructor_enabled = (header->destructor_state == DS_enabled);
  header->destructor_state = DS_already_called;

  /* Call destructor on object. */
  if(destructor_enabled && mp->explicit_destructor != NULL)
  {
    ASAN_POISON_MEMORY_REGION(header, sizeof(Header));
    mp->explicit_destructor(ptr);
    ASAN_UNPOISON_MEMORY_REGION(header, sizeof(Header));
  }

  /* Detach header from allocated chunk list. */
  if(header->prev != NULL)
  {
    ASAN_UNPOISON_MEMORY_REGION(header->prev, sizeof(Header));
    header->prev->next = header->next;
    ASAN_POISON_MEMORY_REGION(header->prev, sizeof(Header));
  }
  if(header->next != NULL)
  {
    ASAN_UNPOISON_MEMORY_REGION(header->next, sizeof(Header));
    header->next->prev = header->prev;
    ASAN_POISON_MEMORY_REGION(header->next, sizeof(Header));
  }
  if(header == mp->allocated_chunks)
  {
    mp->allocated_chunks = mp->allocated_chunks->next;
  }

#ifdef CREGION_ALWAYS_FRESH_MALLOC
  free(header);
#else
  /* Prepend header to released chunk list. */
  header->prev = NULL;
  header->next = mp->released_chunks;
  mp->released_chunks = header;

  if(header->next != NULL)
  {
    ASAN_UNPOISON_MEMORY_REGION(header->next, sizeof(Header));
    header->next->prev = header;
    ASAN_POISON_MEMORY_REGION(header->next, sizeof(Header));
  }
  ASAN_POISON_MEMORY_REGION(mp->released_chunks, mp->chunk_size);
#endif
}
