/** @file
  Implements various functions for error handling.
*/

#include "error-handling.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Prints an error message and terminates the program. It takes the same
  arguments as printf().

  @param format A valid formating string. This string doesn't need to
  contain newlines.
  @param ... Additional arguments.
*/
void die(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);

  fprintf(stderr, "nb: ");
  vfprintf(stderr, format, arguments);
  fprintf(stderr, "\n");

  va_end(arguments);

  exit(EXIT_FAILURE);
}

/** Almost identical to die(), but it also prints a description of the
  current errno value.

  @see die()
*/
void dieErrno(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);

  fprintf(stderr, "nb: ");
  vfprintf(stderr, format, arguments);
  fprintf(stderr, ": %s\n", strerror(errno));

  va_end(arguments);

  exit(EXIT_FAILURE);
}
