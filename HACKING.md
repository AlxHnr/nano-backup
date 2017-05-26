Nano-backups design permits the program to crash at any time without
corrupting the backup. This allows handling errors by terminating with an
error message. For this purpose the functions die() and dieErrno() are
provided.

**Warning**: never call die(), dieErrno() or any safe function from a
function registered with atexit().

## Memory management

Nano-backup uses almost all of its allocated data right until it
terminates. To take advantage of this property and to simplify memory
management, a memory pool is provided. This pool gets freed automatically
when the program terminates, even if this happens via die() or dieErrno().
See the documentation of mpAlloc() for more informations.

Data with a shorter lifetime than the program is not managed by the memory
pool. The drawback of this is that such data will leak if the program
aborts with an error:

```c
void *foo = sMalloc(100);


/* This calls die() on failure and leaks "foo". */
FileStream *reader = sFopenRead("/etc/init.conf");
```
In this case its up to the OS to reclaim the memory.

## Handling strings

Besides usual char pointers, immutable string slices are used for various
operations. A slice is called String and associates a char pointer with a
length. Thus it doesn't need to be null-terminated.

The tricky part is passing such strings to C library functions. Before
doing so, you should make sure that the strings buffer is null-terminated.
To clarify that a function expects a null-terminated String, denote it in
its documentation. Or even better: let the function accept a __const char
*__ instead.

## Testing

The source code comes with its own testing framework. Here is the example
test program `"test/foo.c"`:

```c
#include "test.h"

int main(void)
{
  testGroupStart("some asserts");

  assert_true(5 + 5 == 10);

  assert_error(sMalloc(0), "unable to allocate 0 bytes");

  testGroupEnd();
}
```

Now "foo" must be added to the makefiles test target. This has to be done
manually to ensure that tests run in the correct order.
