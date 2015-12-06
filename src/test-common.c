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

/** @file
  Implements various testing functions shared across tests.
*/

#include "test-common.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "safe-wrappers.h"
#include "error-handling.h"

/** Determines the current working directory.

  @return A String which should not freed by the caller.
*/
String getCwd(void)
{
  int old_errno = errno;
  size_t capacity = 128;
  char *buffer = sMalloc(capacity);
  char *result = NULL;

  do
  {
    result = getcwd(buffer, capacity);
    if(result == NULL)
    {
      if(errno != ERANGE)
      {
        dieErrno("failed to get current working directory");
      }

      capacity = sSizeMul(capacity, 2);
      buffer = sRealloc(buffer, capacity);
      errno = old_errno;
    }
  }while(result == NULL);

  String cwd = strCopy(str(buffer));
  free(buffer);

  return cwd;
}

