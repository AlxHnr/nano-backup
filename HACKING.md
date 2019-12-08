Backups are atomic by design. A backup is either completed or stays at its
current state. Even if the program crashes (e.g. segfault).

## Handling errors

Errors are handled by calling `die()` or `dieErrno()`:

```c
#include "error-handling.h"

if(some_function() == -1)
{
  die("failed to call some function");
}
```

## Managing memory

Memory is managed via
[regions](https://en.wikipedia.org/wiki/Region-based_memory_management),
which get freed automatically when the program terminates. This memory will
also be released if the program terminates by calling `die()` or
`dieErrno()`. See the examples in `third-party/CRegion/README.md` for more
informations.

## Handling strings

Strings are immutable slices which don't own the memory they point to:

```c
#include "str.h"

String path = strWrap("/etc/conf.d/boot.conf");

String dirname = strSplitPath(path).head;

/* Strings are not always null-terminated. Use the function cStr() to get
   a terminated C string. It will copy and terminate the given String if
   required. It takes a pointer to growable memory as the second
   argument. See the examples in `third-party/CRegion/README.md` for more
   informations. */
char *reusable_buffer = NULL;

raw_c_function(cStr(dirname, &reusable_buffer));
```

## Testing

The source code comes with its own testing framework. Here an example test
program:

```c
#include "test.h"

#include "safe-wrappers.h"

int main(void)
{
  testGroupStart("some asserts");

  assert_true(5 + 5 == 10);

  assert_error(sMalloc(0), "unable to allocate 0 bytes");

  testGroupEnd();
}
```

This file must be stored in the test directory, e.g. `"test/foo.c"`. Now
"foo" must be added to `"test/run-tests.sh"`. This has to be done manually
to ensure that tests run in the correct order.

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
data generated during the test should be stored. Additionally the directory
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
down into phases. This can be done by moving all the files described above
into a directory named `"1/"`. The 1 stands for the first phase. Additional
directories named `"2/"`, `"3/"`, etc. can be created. Each phase will
reuse the following files and directories from preceding phases, if they
are not overridden:

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
