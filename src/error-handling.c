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
  Implements various functions for error handling.
*/

#include "error-handling.h"

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/** Prints an error message and terminates the program. It takes the same
  arguments as printf().

  @param format A valid formating string. This string doesn't need to
  contain newlines.
  @param ... Additional arguments.
*/
void die(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);

  fprintf(stderr, "nb: ");
  vfprintf(stderr, format, arguments);
  fprintf(stderr, "\n");

  va_end(arguments);

  exit(EXIT_FAILURE);
}

/** Almost identical to die(), but it also prints a description of the
  current errno value.

  @see die()
*/
void dieErrno(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);

  fprintf(stderr, "nb: ");
  vfprintf(stderr, format, arguments);
  fprintf(stderr, ": %s\n", strerror(errno));

  va_end(arguments);

  exit(EXIT_FAILURE);
}
