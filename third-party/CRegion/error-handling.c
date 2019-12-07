/** @file
  Implements a function for handling errors by terminating with failure.
*/

#include "error-handling.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/** Prints an error message and terminates the program. It takes the same
  arguments as printf().

  @param format A valid formatting string. This string doesn't need to
  contain newlines.
  @param ... Additional arguments.
*/
void CR_ExitFailure(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);

  fprintf(stderr, "CRegion: ");
  vfprintf(stderr, format, arguments);
  fprintf(stderr, "\n");

  va_end(arguments);

  exit(EXIT_FAILURE);
}
