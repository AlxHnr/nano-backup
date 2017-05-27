Nano-backups design permits the program to crash at any time without
corrupting the backup. This allows handling errors by terminating with an
error message. For this purpose the functions die() and dieErrno() are
provided.

**Warning**: never call die(), dieErrno() or any safe function from a
function registered with atexit().

The source code conforms strictly to C99 and POSIX.1-2001 with the XSI
extension lchown(). When using a library function that can fail or result
in undefined/implementation dependent behaviour, write a safe wrapper for
it. See safe-wrappers.h for examples.

## Memory management

Nano-backup uses almost all of its allocated data right until it
terminates. To take advantage of this property and to simplify memory
management, a memory pool is provided. This pool gets freed automatically
when the program terminates, even if this happens via die() or dieErrno().
See the documentation of mpAlloc() for more informations.

Data with a shorter lifetime than the program has to be managed manually.
The drawback of this is that such data will leak if the program aborts with
an error:

```c
void *foo = sMalloc(100);


/* This calls die() on failure and leaks "foo". */
FileStream *reader = sFopenRead("/etc/init.conf");
```
In this case its up to the OS to reclaim the memory.

**Note**: Use sSizeAdd() and sSizeMul() to calculate the amount of data to
allocate. This prevents unexpected behaviour resulting from overflows.

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

### Testing the final executable

Full program tests are located in `"test/full program
tests/CATEGORY/NAME/"`. The directory `"NAME/"` can contain the scripts
`"init.sh"`, `"run.sh"` and `"clean.sh"`. These scripts are run inside
`"NAME/"` by being passed to `"/bin/sh -e"`. Thus every failing command
causes the entire test to fail. If one of these scripts doesn't exist, a
fallback alternative will be used. These are located in `"test/full program
tests/fallback targets/"`.

From inside these scripts the following variables are accessible:

Variable      | Description
--------------|---------------
NB            | The path to the nb executable.
PROJECT\_PATH | The path to the projects root directory which contains the Makefile, etc.
PHASE\_PATH   | The path to the current phase relative to `"NAME/"`. Defaults to `"."`.

It is usually not required to implement `"init.sh"`, `"run.sh"` or
`"clean.sh"`. The fallback scripts should be flexible enough and can be
complemented with scripts like `"pre-run.sh"` or `"post-init.sh"`, etc. The
fallback scripts will create the directory `"generated/"`, where all the
data generated during the test should be stored. Additionally the
`"generated/repo/"` will be created and contains the repository to test.

The fallback scripts _must_ be configured by creating one of the following
files inside `"NAME/"`:

File name                 | Description
--------------------------|--
expected-output           | The expected program output with paths relative to `"NAME/"`.
expected-output-test-data | The expected program output with paths relative to `"$PROJECT_PATH/test/data"`.

Additionally the following optional files can be created inside `"NAME/"`:

File name           | Description
--------------------|------------
input               | This files content gets piped into nb. Defaults to "yes".
arguments           | This files content gets passed to nb as argument. Defaults to "generated/repo".
exit-status         | The expected exit status of nb. Defaults to 0.
config              | The config file to use with paths relative to `"NAME/"`.
config-test-data    | The config file to use with paths relative to `"$PROJECT_PATH/test/data"`.
expected-repo-files | A list of files expected to be inside the repository after the test. Paths are relative to `"generated/repo/"`.

#### Multiple test phases

To test multiple backups using the same repository, the test must be broken
down into phases. This can be done by moving all files described above into
a directory named `"1/"`. The 1 stands for the first phase. Additional
directories named `"2/"`, `"3/"`, etc. can be created. Each phase will
reuse the following files and directories from preceding phases:

* generated/
* generated/repo/
* config
* config-test-data
* expected-output
* expected-output-test-data
* expected-repo-files

The following files are specific to each phase and will not be reused from
preceding phases. If they are not provided, their defaults will be used:

* input
* arguments
* exit-status

**Note**: The scripts `"pre-clean.sh"`, `"clean.sh"` and `"post-clean.sh"`
will be run only before the first phase and after the last phase.
