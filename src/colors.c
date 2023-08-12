/** @file
  Implements functions for printing colored text.
*/

#include "colors.h"

#include <stdarg.h>

#include "safe-wrappers.h"

/** Contains color codes in the same order as TextColor, which can be used
  as index. */
static const char *color_codes[] = {
  "0;31", "1;31", /* Red, bold red. */
  "0;32", "1;32", /* Green, bold green. */
  "0;33", "1;33", /* Yellow, bold yellow. */
  "0;34", "1;34", /* Blue, bold blue. */
  "0;35", "1;35", /* Magenta, bold magenta. */
  "0;36", "1;36", /* Cyan, bold cyan. */
  "0;37", "1;37", /* White, bold white. */
};

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
  bool colorize = sIsTTY(stream);

  if(colorize) fprintf(stream, "\033[%sm", color_codes[color]);
  vfprintf(stream, format, arguments);
  if(colorize) fprintf(stream, "\033[0m");

  va_end(arguments);
}
