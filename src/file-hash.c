/** @file
  Implements functions for calculating the hash of a file.
*/

#include "file-hash.h"

#include <stdlib.h>

#include "buffer.h"
#include "safe-wrappers.h"
#include "error-handling.h"

static Buffer *io_buffer = NULL;

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

  bufferEnsureCapacity(&io_buffer, blocksize);
  char *buffer = io_buffer->data;

  SHA_CTX context;
  SHA1_Init(&context);

  while(bytes_left > 0)
  {
    size_t bytes_to_read = bytes_left > blocksize ? blocksize : bytes_left;

    sFread(buffer, bytes_to_read, stream);
    SHA1_Update(&context, buffer, bytes_to_read);
    bytes_left -= bytes_to_read;
  }

  bool stream_not_at_end = sFbytesLeft(stream);
  sFclose(stream);

  if(stream_not_at_end)
  {
    die("file changed while calculating hash: \"%s\"", path);
  }

  SHA1_Final(hash, &context);
}
