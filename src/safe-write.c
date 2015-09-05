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
  Implements functions for safe writing of files into backup repositories.
*/

#include "safe-write.h"

#include <stdio.h>

struct SafeWriteHandle
{
  /** The full or relative path to the directory to which the current
    handle belongs. */
  const char *dir_path;

  /** The path to the temporary file. */
  char *tmp_file_path;

  /** The stream to write to the temporary file. */
  FILE *tmp_file_stream;

  /** The final filepath, to which the temporary file gets renamed. */
  char *dest_path;

  /** The name or path of the file represented by this handle. It differs
    from @link dest_path @endlink and contains a human readable name/path
    for useful error printing. */
  const char *real_file_path;
};
