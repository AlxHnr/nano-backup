#include "file-hash.h"

#include <stdlib.h>

#include "BLAKE2/blake2.h"
#include "CRegion/alloc-growable.h"

#include "error-handling.h"
#include "safe-wrappers.h"

/** Calculates the hash of a file.

  @param path The full or relative path to the file for which the hash
  should be calculated.
  @param stats A stat struct with informations about the file. Required to
  determine the files size and optimal buffer size.
  @param hash_out The location to which the hash will be written. Its size
  must be at least FILE_HASH_SIZE.
*/
void fileHash(StringView path, const struct stat stats, uint8_t *hash_out)
{
  const size_t blocksize = stats.st_blksize;
  uint64_t bytes_left = stats.st_size;
  FileStream *stream = sFopenRead(path);

  static unsigned char *buffer = NULL;
  buffer = CR_EnsureCapacity(buffer, blocksize);

  blake2b_state state;
  blake2b_init(&state, FILE_HASH_SIZE);

  while(bytes_left > 0)
  {
    const size_t bytes_to_read =
      bytes_left > blocksize ? blocksize : bytes_left;

    sFread(buffer, bytes_to_read, stream);
    blake2b_update(&state, buffer, bytes_to_read);
    bytes_left -= bytes_to_read;
  }

  const bool stream_not_at_end = sFbytesLeft(stream);
  sFclose(stream);

  if(stream_not_at_end)
  {
    die("file changed while calculating hash: \"%s\"", path.content);
  }

  blake2b_final(&state, hash_out, FILE_HASH_SIZE);
}
