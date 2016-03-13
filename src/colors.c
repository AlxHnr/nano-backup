/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

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
