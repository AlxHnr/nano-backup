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

#include <regex.h>

#include "memory-pool.h"
#include "safe-wrappers.h"

struct StringMatcher
{
  /** The expression as a string. It will be used for matching if is_regex
    is false. Otherwise it will contain the source expression of the
    compiled pattern. */
  String expression;

  /** True, if this expression is a compiled regex. */
  bool is_regex;

  /** Contains either a compiled expression, or is undefined if is_regex is
    false. */
  regex_t pattern;

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
static size_t regex_pool_allocated = 0;

/** Builds a StringMatcher from a String. It will be allocated inside the
  internal memory pool.

  @param expression A string which will be used for matching. This string
  will be referenced by the returned StringMatcher, and should not be
  freed, unless the StringMatcher is not longer used.
  @param line_nr The number of the line, on which the expression was
  defined inside the config file.

  @return A StringMatcher, which should not be freed by the caller. It will
  keep a reference to the given expression.
*/
StringMatcher *strmatchString(String expression, size_t line_nr)
{
  StringMatcher *matcher = mpAlloc(sizeof *matcher);

  matcher->expression = expression;
  matcher->is_regex = false;
  matcher->line_nr = line_nr;
  matcher->has_matched = false;

  return matcher;
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
