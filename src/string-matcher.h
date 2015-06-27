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
  @file string-matcher.h Defines functions for simple string matching.
*/

#ifndef _NANO_BACKUP_STRING_MATCHER_H_
#define _NANO_BACKUP_STRING_MATCHER_H_

#include <stddef.h>
#include <stdbool.h>

#include "string-utils.h"

typedef struct StringMatcher StringMatcher;

extern StringMatcher *strmatchString(String expression, size_t line_nr);
extern StringMatcher *strmatchRegex(String expression, size_t line_nr);

extern bool strmatch(StringMatcher *matcher, const char *string);
extern bool strmatchHasMatched(StringMatcher *matcher);
extern size_t strmatchLineNr(StringMatcher *matcher);
extern String strmatchGetString(void);

#endif
