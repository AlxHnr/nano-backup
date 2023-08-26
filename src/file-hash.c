#include "file-hash.h"

#include <stdlib.h>

#include "BLAKE2/blake2.h"
#include "CRegion/alloc-growable.h"

#include "error-handling.h"
#include "safe-wrappers.h"

/** Calculates the hash of a file.

  @param stats Informations about the specified file.
  @param hash_out The location to which the hash will be written. Its size
  must be at least FILE_HASH_SIZE.
  @param progress_callback Will be called for each processed block. Can be
  NULL. Will never be called if the specified file is empty.
  @param callback_user_data Will be passed to `progress_callback`.
*/
void fileHash(StringView filepath, struct stat stats, uint8_t *hash_out,
              HashProgressCallback progress_callback,
              void *callback_user_data)
{
  const size_t blocksize = stats.st_blksize;
  uint64_t bytes_left = stats.st_size;
  FileStream *stream = sFopenRead(filepath);

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

    if(progress_callback != NULL)
    {
      progress_callback(bytes_to_read, callback_user_data);
    }
  }

  const bool stream_not_at_end = sFbytesLeft(stream);
  sFclose(stream);

  if(stream_not_at_end)
  {
    die("file changed while calculating hash: \"" PRI_STR "\"",
        STR_FMT(filepath));
  }

  blake2b_final(&state, hash_out, FILE_HASH_SIZE);
}
