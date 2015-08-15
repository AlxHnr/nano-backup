By design, nano-backup will never break a backup repository. Thus it can
crash at any time without data loss. Here is a list of topics that must be
considered when working with the source code:

* [Error handling](#errorhandling)
* [Memory management](#memorymanagement)
* [NULL](#null)
* [String handling](#stringhandling)
* [Testing](#testing)

## Error handling

Since the backup repository is guaranteed not to break, errors are handled
by terminating with an error message. This should only be done trough the
functions die() and dieErrno(). The error message shouldn't contain a
newline and must not end with a period. Everything is an error, that
prevents to achieve the users exact goal. One example is to backup a file,
which can't be read due to a lack of permissions. Another example would be
if a file gets modified while being backed up and doesn't match the changes
confirmed by the user anymore. Nano-backup must be predictable and precise.
Take the time to write good and conclusive error messages. Errors like
`"failed to backup foo.txt"` are useless, because they don't explain _why_
it fails.

To simplify error handling even further, wrappers for commonly used
functions are provided. See safe-wrappers.h. **Warning**: never call die(),
dieErrno() or any failsafe function from a function registered with
atexit().

## NULL

NULL is an error. Try to avoid it whenever you can. Write a wrapper
function that handles NULL with an appropriate error message. There are
very few places where NULL occurs in the codebase. One example would be to
terminate linked lists, or to denote optional variables. Places where NULL
occurs are explicitly denoted as such in the documentation.

## Memory management

Most data lives until the backup is completed and the program terminates.
Nano-backup provides a fast, internal memory pool for such structs. This
pool will be freed automatically when the program terminates. Allocating
memory from this pool can be done with mpAlloc(). This memory should not be
freed manually and data structures allocated inside this pool are denoted
appropriately in their documentation.

Sometimes data has a shorter lifetime than the program. Such data will not
be allocated inside the internal memory pool and must be freed explicitly.
This data is denoted appropriately in the documentation and may not be
freed if the program terminates with an error. In this case its up to the
OS to reclaim the programs memory.

## String handling

Strings in C are broken and expected to be null-terminated. Even a simple
split operation may require allocating a new string, or mutating the actual
string in place. To make string operations simpler and cheaper, nano-backup
uses immutable string slices. These slices don't own the memory they point
to, which allows multiple strings to point to the same data. A string can
be split just by pointing into another string. This will never break
foreign strings, because a slice can't mutate the data to which it points
to.

The only tricky thing is the fact, that a string slice may not point to a
null-terminated char array. Before passing a string slice to a C library
function, make sure that the documentation guarantees that it is
null-terminated.

## Testing
