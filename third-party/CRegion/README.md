[![travis](https://travis-ci.org/AlxHnr/CRegion.svg?branch=master)](https://travis-ci.org/AlxHnr/CRegion)
[![codecov](https://codecov.io/github/AlxHnr/CRegion/coverage.svg?branch=master)](https://codecov.io/github/AlxHnr/CRegion?branch=master)
[![license](https://licensebuttons.net/p/zero/1.0/88x31.png)](LICENSE)

A free, public-domain
[region](https://en.wikipedia.org/wiki/Region-based_memory_management)
implementation without any dependencies. It conforms strictly to C99 and is
valgrind-clean. Allocation failures and arithmetic overflows are handled by
calling [exit()](src/error-handling.c).

Copy the files from `src/` into your project and add them to your build
script.

## Examples

```c
#include "region.h"

CR_Region *r = CR_RegionNew();

char *data = CR_RegionAlloc(r, 1024);

CR_RegionRelease(r); /* If omitted, it will be released trough atexit() */
```

Callbacks can be attached to regions and will be called when the region
gets released:

```c
void cleanup(void *data) { /* ... */ }

Foo *foo = createFoo();

CR_RegionAttach(r, cleanup, foo);
```

Memory allocated via `CR_RegionAlloc()` has a fixed size and can not be
reallocated. Use `CR_RegionAllocGrowable()` to get growable memory:

```c
#include "alloc-growable.h"

char *buffer = CR_RegionAllocGrowable(r, 24);

/* Grow the buffer if needed. */
buffer = CR_EnsureCapacity(buffer, 128);
```

In the example above the lifetime of _buffer_ will be bound to the region
_r_. If _r_ gets released, _buffer_ will also be released. To bind a buffer
to the lifetime of the entire program, initialize it to NULL:

```c
static char *buffer = NULL;

buffer = CR_EnsureCapacity(buffer, 128);
```

Objects with a very short lifetime can be allocated using a memory pool,
which allows reusing memory in a region:

```c
#include "mempool.h"

CR_Mempool *int_pool = CR_MempoolNew(r, sizeof(int), NULL, NULL);

int *value = CR_MempoolAlloc(int_pool);
*value = -12;
```

The lifetime of memory returned from the pool is bound to pool itself,
which in turn is bound to the region. Objects can be released manually:

```c
CR_DestroyObject(value);
```

Objects allocated by the pool can have destructors. To do so, two callbacks
can to be provided to the mempool.

The first one will be called when destroying objects explicitly by using
`CR_DestroyObject()`. This callback is allowed to handle errors by calling
`exit()`.

The second callback will be called for objects that have not been destroyed
explicitly. It will be invoked when the mempool gets released together with
its associated region. This callback is _not_ allowed to call `exit()`,
because it may be called while the program is terminating.

The mempool guarantees that _only one_ of these callbacks will be called
for the same object.

```c
/* This destructor will be called when an object is passed to
  CR_DestroyObject(). */
int closeFile(void *data)
{
  FileHandle *file = data;
  if(syncToDiskAndClose(file) == ERROR)
  {
    CR_ExitFailure("failed to write important file");
  }

  /* This function returns an int, to have a signature different from the
     other destructor. The returned value will be ignored. */
  return 0;
}

/* This will be called for objects which couldn't be destroyed explicitly
  (e.g. the program called exit() prematurely). It is not allowed to call
  exit() and will be invoked when the mempool (and its owning region) get
  released. */
void freeFile(void *data)
{
  FileHandle *file = data;
  (void)syncToDiskAndClose(file);
}

CR_Mempool *file_pool = CR_MempoolNew(r, sizeof(FileHandle), closeFile, freeFile);

FileHandle *file = CR_MempoolAlloc(file_pool);
file->stream = openStream("/dev/null");

CR_EnableObjectDestructor(file); /* Must be done explicitly after the
                                    object is fully constructed */
```

# Debugging and sanitizing

This library allocates mostly from continuous memory, which makes it
impossible for debugging tools to detect overflows. In order to use such
tools, continuous memory has to be disabled. This can be achieved by
defining `CREGION_ALWAYS_FRESH_MALLOC` during compilation. Doing so causes
CRegion to return new, fresh memory from raw malloc on every single
allocation.

## Caveats

Runtime leak-detectors are not useful because CRegion will clean up
everything when the program terminates. Changing this behaviour would
require invasive modifications to the library _and_ to code using this
library. This would break the way CRegion is intended to be used. CRegion
is not thread-safe.
