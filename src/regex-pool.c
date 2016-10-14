/*
  Copyright (c) 2016 Alexander Heinrich

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
  Implements a regex pool which gets freed automatically when the program
  terminates.
*/

#include "regex-pool.h"

#include <stdlib.h>

#include "memory-pool.h"
#include "safe-wrappers.h"
#include "error-handling.h"

static regex_t **regex_pool = NULL;
static size_t regex_pool_used = 0;
static size_t regex_pool_capacity = 0;

/** Frees the entire regex pool. */
static void freeRegexPool(void)
{
  for(size_t index = 0; index < regex_pool_used; index++)
  {
    regfree(regex_pool[index]);
    free(regex_pool[index]);
  }

  free(regex_pool);
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
  /* Grow regex pool if its used up. */
  if(regex_pool_used == regex_pool_capacity)
  {
    if(regex_pool == NULL) atexit(freeRegexPool);

    size_t new_pool_length =
      regex_pool_capacity == 0 ? 4 : sSizeMul(regex_pool_capacity, 2);
    size_t new_pool_size = sSizeMul(sizeof *regex_pool, new_pool_length);

    regex_pool = sRealloc(regex_pool, new_pool_size);
    regex_pool_capacity = new_pool_length;
  }

  regex_t *regex = sMalloc(sizeof *regex);
  int error = regcomp(regex, expression, REG_EXTENDED | REG_NOSUB);

  if(error != 0)
  {
    size_t error_length = regerror(error, regex, NULL, 0);
    char *error_str = mpAlloc(error_length);
    regerror(error, regex, error_str, error_length);
    free(regex);

    die("%s: line %zu: %s: \"%s\"", file_name,
        line_nr, error_str, expression);
  }

  /* Store reference in regex pool. */
  regex_pool[regex_pool_used] = regex;
  regex_pool_used++;

  return regex;
}
