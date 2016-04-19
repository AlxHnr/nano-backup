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
  Implements functions for calculating the hash of a file.
*/

#include "file-hash.h"

#include <stdlib.h>

#include "safe-wrappers.h"
#include "error-handling.h"

/** Calculates the hash of a file.

  @param path The full or relative path to the file for which the hash
  should be calculated.
  @param stats A stat struct with informations about the file. Required to
  determine the files size and optimal buffer size.
  @param hash The location to which the hash will be written. Its size must
  be at least FILE_HASH_SIZE.
*/
void fileHash(const char *path, struct stat stats, uint8_t *hash)
{
  size_t blocksize    = stats.st_blksize;
  uint64_t bytes_left = stats.st_size;
  FileStream *stream  = sFopenRead(path);
  char *buffer        = sMalloc(blocksize);

  SHA_CTX context;
  SHA1_Init(&context);

  while(bytes_left > 0)
  {
    size_t bytes_to_read = bytes_left > blocksize ? blocksize : bytes_left;

    sFread(buffer, bytes_to_read, stream);
    SHA1_Update(&context, buffer, bytes_to_read);
    bytes_left -= bytes_to_read;
  }

  free(buffer);
  bool stream_not_at_end = sFbytesLeft(stream);
  sFclose(stream);

  if(stream_not_at_end)
  {
    die("file changed while calculating hash: \"%s\"", path);
  }

  SHA1_Final(hash, &context);
}
