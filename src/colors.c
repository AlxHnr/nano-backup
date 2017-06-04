/** @file
  Implements functions for printing colored text.
*/

#include "colors.h"

#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdbool.h>

#include "error-handling.h"

/** Contains color codes in the same order as TextColor, which can be used
  as index. */
static const char *color_codes[] =
{
  "0;31", "1;31",
  "0;32", "1;32",
  "0;33", "1;33",
  "0;34", "1;34",
  "0;35", "1;35",
  "0;36", "1;36",
  "0;37", "1;37",
};

/** Checks if the given stream may support ANSI escape color codes.

  @param stream The file stream which should be checked. If the given
  stream is neither stdout nor stderr, false will be returned.

  @return True if the given stream is either stdout or stderr and belongs
  to a TTY.
*/
static bool maySupportEscapeColors(FILE *stream)
{
  if(stream != stdout && stream != stderr)
  {
    return false;
  }

  int descriptor = fileno(stream);
  if(descriptor == -1)
  {
    dieErrno("failed to get file descriptor from stream");
  }

  /* Preserve errno in case isatty() returns 0. */
  int old_errno = errno;
  int is_tty = isatty(descriptor);
  errno = old_errno;

  return is_tty == 1;
}

/** A colorized wrapper around fprintf(). If the given stream is neither
  stdout nor stderr, or does not belong to a TTY, the text will be printed
  without colors.

  @param stream The file stream which should be used for writing the
  formatted text.
  @param color The color attributes which the text should have.
  @param format The format string. See the documentation of printf().
  @param ... The format arguments. See the documentation of printf().
*/
void colorPrintf(FILE *stream, TextColor color, const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  bool colorize = maySupportEscapeColors(stream);

  if(colorize) fprintf(stream, "\033[%sm", color_codes[color]);
  vfprintf(stream, format, arguments);
  if(colorize) fprintf(stream, "\033[0m");

  va_end(arguments);
}
