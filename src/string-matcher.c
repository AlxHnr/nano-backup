/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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

/**
  @file string-matcher.c Implements functions for simple string matching.
*/

#include "string-matcher.h"

#include <stdlib.h>
#include <string.h>

#include <regex.h>

#include "memory-pool.h"
#include "safe-wrappers.h"
#include "error-handling.h"

struct StringMatcher
{
  /** The expression as a string. It will be used for matching if is_regex
    is false. Otherwise it will contain the source expression of the
    compiled pattern. */
  String expression;

  /** True, if this expression is a compiled regex. */
  bool is_regex;

  /** Contains either a compiled expression, or is undefined if is_regex is
    false. It points into the internal regex pool of this module. */
  regex_t *pattern;

  /** The number of the line in the config file, on which this expression
    was initially defined. */
  size_t line_nr;

  /** True, if the expression actually matched an existing file during
    its lifetime. */
  bool has_matched;
};

/** All StringMatcher are allocated inside the programs memory pool and
 live as long as the entire program. But since regex_t variables are
 allocated separately, they must be tracked and freed at exit: */
static regex_t **regex_pool = NULL;
static size_t regex_pool_used = 0;
static size_t regex_pool_length = 0;

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

/** Surrounds the given String with '^' and '$', if it wasn't already.

  @param string A String.

  @return A null terminated string, which must be freed by the caller using
  free().
*/
static char *wrapRegex(String string)
{
  bool needs_bol = string.str[0] != '^';
  bool needs_eol =
    (string.length == 0 || string.str[string.length - 1] != '$');

  size_t new_size = sSizeAdd(string.length, needs_bol + needs_eol);
  char *new_string = sMalloc(sSizeAdd(new_size, 1));

  if(needs_bol) new_string[0] = '^';
  if(needs_eol) new_string[new_size - 1] = '$';
  new_string[new_size] = '\0';

  memcpy(&new_string[needs_bol], string.str, string.length);

  return new_string;
}

/** Builds a StringMatcher from a String. It will be allocated inside the
  internal memory pool.

  @param expression A string which will be used for matching. This string
  will be referenced by the returned StringMatcher and should not be freed
  or modified, unless the StringMatcher is not used anymore.
  @param line_nr The number of the line, on which the expression was
  defined inside the config file.

  @return A StringMatcher, which should not be freed by the caller. It will
  keep a reference to the given expression.
*/
StringMatcher *strmatchString(String expression, size_t line_nr)
{
  StringMatcher *matcher = mpAlloc(sizeof *matcher);

  /* Copy the given expression into a String with const members. */
  memcpy(&matcher->expression, &expression, sizeof(expression));

  matcher->is_regex = false;
  matcher->line_nr = line_nr;
  matcher->has_matched = false;

  return matcher;
}

/** Like strmatchString(), but will compile the given expression. If
  compiling the expression fails, the program will be terminated with an
  error message.

  @param expression A String containing a valid POSIX extended regex. This
  function will ensure, that the given string is surrounded by '^' and '$'
  before it gets compiled. This string will be referenced by the returned
  StringMatcher and should not be freed or modified, unless the
  StringMatcher is not used anymore.
  @param line_nr The number of the line, on which the expression was
  defined inside the config file.

  @return A StringMatcher, which should not be freed by the caller. It will
  keep a reference to the given expression.
*/
StringMatcher *strmatchRegex(String expression, size_t line_nr)
{
  StringMatcher *matcher = mpAlloc(sizeof *matcher);

  /* Copy the given expression into a String with const members. */
  memcpy(&matcher->expression, &expression, sizeof(expression));

  matcher->is_regex = true;
  matcher->line_nr = line_nr;
  matcher->has_matched = false;

  /* Ensure that there is enough space in the regex pool. */
  if(regex_pool_used == regex_pool_length)
  {
    if(regex_pool == NULL) atexit(freeRegexPool);

    size_t new_pool_length =
      regex_pool_length == 0 ? 8 : sSizeMul(regex_pool_length, 2);
    size_t new_pool_size = sSizeMul(new_pool_length, sizeof *regex_pool);

    regex_pool = sRealloc(regex_pool, new_pool_size);
    regex_pool_length = new_pool_length;
  }

  /* Wrap and compile regular expression. */
  regex_t *pattern = sMalloc(sizeof *pattern);
  char *wrapped_string = wrapRegex(expression);
  int error = regcomp(pattern, wrapped_string, REG_EXTENDED | REG_NOSUB);
  free(wrapped_string);

  if(error != 0)
  {
    size_t error_length = regerror(error, pattern, NULL, 0);
    char *error_str = mpAlloc(error_length);
    regerror(error, pattern, error_str, error_length);
    free(pattern);

    /* The strCpy() below is required, to guarantee that a null-terminated
       string is passed to die(). */
    die("config: line %zu: %s: \"%s\"", line_nr, error_str,
        strCopy(expression).str);
  }

  regex_pool[regex_pool_used] = pattern;
  matcher->pattern = pattern;
  regex_pool_used++;

  return matcher;
}

/** Returns true, if the given matcher matches the specified string.

  @param matcher A valid StringMatcher.
  @param string A null terminated string.

  @return True or false.
*/
bool strmatch(StringMatcher *matcher, const char *string)
{
  bool match;

  if(matcher->is_regex)
  {
    match = regexec(matcher->pattern, string, 0, NULL, 0) == 0;
  }
  else
  {
    match = strncmp(matcher->expression.str, string,
                    matcher->expression.length) == 0 &&
      string[matcher->expression.length] == '\0';
  }

  matcher->has_matched |= match;

  return match;
}

/** Returns true, if the given StringMatcher has successfully matched a
  string in its lifetime. */
bool strmatchHasMatched(StringMatcher *matcher)
{
  return matcher->has_matched;
}

/** Returns The number of the line, on which the match was defined in the
  config file. */
size_t strmatchLineNr(StringMatcher *matcher)
{
  return matcher->line_nr;
}

/** Returns the matching expression of the given StringMatcher as a String.

  @return A String, which should not be freed by the caller. This string is
  not owned by the matcher.
*/
String strmatchGetExpression(StringMatcher *matcher)
{
  return matcher->expression;
}
