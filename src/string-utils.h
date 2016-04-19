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
  Declares various helper functions for manipulating strings.
*/

#ifndef NANO_BACKUP_STRING_UTILS_H
#define NANO_BACKUP_STRING_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** A simple struct associating a string with its length. This struct
  doesn't own the memory it points to and can be used for efficient string
  slicing.
*/
typedef struct
{
  /** A pointer to the beginning of the string. It doesn't need to be null
    terminated, but be careful when passing it to C library functions. */
  const char *const str;

  /** The length of the string. It can be shorter than the actual string
    stored in "str", but be careful when passing it to C library
    functions. */
  const size_t length;
}String;

/** A struct representing a string splitting. */
typedef struct
{
  String head; /**< The part before the split. */
  String tail; /**< The part after the split. */
}StringSplit;

extern String str(const char *string);
extern String strCopy(String string);

extern bool strCompare(String a, String b);
extern bool strWhitespaceOnly(String string);
extern uint32_t strHash(String string);

extern String strRemoveTrailingSlashes(String string);
extern String strAppendPath(String path, String filename);
extern StringSplit strSplitPath(String path);

#endif
