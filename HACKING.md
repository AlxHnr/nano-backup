By design, nano-backup will never break a backup repository. Thus it can
crash at any time without data loss. Here is a list of topics that must be
considered when working with the source code:

* [Error handling](#errorhandling)
* [Memory management](#memorymanagement)
* [Handling strings](#handlingstrings)
* [Testing](#testing)

## Error handling

Since the backup repository is guaranteed not to break, errors are handled
by terminating with an error message. This should only be done trough the
functions die() and dieErrno(). The error message shouldn't contain a
newline and must not end with a period. Everything is an error, that
prevents to achieve the users exact goal. One example would be if a file
gets modified during backup and doesn't match the changes confirmed by the
user anymore. Take the time to write good and conclusive error messages.
Errors like `"failed to backup foo.txt"` are useless, because they don't
explain _why_ it fails.

To simplify error handling even further, wrappers for commonly used
functions are provided. See safe-wrappers.h. **Warning**: never call die(),
dieErrno() or any failsafe function from a function registered with
atexit().

## Memory management

Most data lives until the backup is completed and the program terminates.
Nano-backup provides a fast, internal memory pool for such data. This pool
will be freed automatically when the program terminates. Allocating memory
from this pool can be done with mpAlloc(). This memory should not be freed
manually.

Sometimes data has a shorter lifetime than the program. Such data will not
be allocated inside the internal memory pool and must be freed explicitly.
This data is denoted appropriately in the documentation and may not be
freed if the program terminates with an error. In this case its up to the
OS to reclaim the programs memory.

## Handling strings

Strings in C are broken and expected to be null-terminated. Even a simple
split operation may require allocating a new string, or mutating the actual
string in place. To make string operations simpler and cheaper, nano-backup
uses immutable string slices. These slices don't own the memory they point
to, which allows multiple strings to point to the same data. A string can
be split just by pointing into another string. This will never break
foreign strings, because a slice can't mutate the data to which it points
to.

The tricky part is passing such strings to C library functions. Before
doing so, you should make sure that the strings buffer is null-terminated.
To clarify that a function expects a null-terminated string, denote it in
its documentation. Or even better: let the function accept a __const char
*__ instead of a String.

## Testing

Handling errors by terminating the program is not helpful when testing
code. Sometimes a test must assert that errors get raised properly. For
this purpose nano-backup ships its own testing functions. Test programs are
linked against a special implementation of die() and dieErrno(), which
longjump back into the last assert statement. See the documentation of
test.h.
