/** @file
  Implements a regex pool which gets freed automatically when the program
  terminates.
*/

#include "regex-pool.h"

#include <stdlib.h>

#include "CRegion/global-region.h"
#include "CRegion/region.h"

#include "error-handling.h"

/** @param data Pointer to a regex_t object to be released. */
static void releaseRegex(void *data)
{
  regfree(data);
}

/** Compiles the given regular expression and terminates the program on
  failure.

  @param expression The expression to compile.
  @param file_name The name of the file to show in the error message.
  @param line_nr The line number in the file at which the regular
  expression was found. Needed for printing useful error messages.

  @return A regex_t which should not be freed by the caller.
*/
const regex_t *rpCompile(const char *expression,
                         const char *file_name,
                         size_t line_nr)
{
  regex_t *regex = CR_RegionAlloc(CR_GetGlobalRegion(), sizeof(*regex));
  const int error = regcomp(regex, expression, REG_EXTENDED | REG_NOSUB);

  if(error != 0)
  {
    const size_t error_length = regerror(error, regex, NULL, 0);
    char *error_str = CR_RegionAlloc(CR_GetGlobalRegion(), error_length);
    regerror(error, regex, error_str, error_length);
    die("%s: line %zu: %s: \"%s\"", file_name,
        line_nr, error_str, expression);
  }

  CR_RegionAttach(CR_GetGlobalRegion(), releaseRegex, regex);

  return regex;
}
