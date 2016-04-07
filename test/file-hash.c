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
  Tests functions for calculating the hash of a file.
*/

#include "file-hash.h"

#include "test.h"
#include "safe-wrappers.h"

/** Simplified wrapper around fileHash(). */
static void fileHashWrapper(const char *path, uint8_t *hash)
{
  fileHash(path, sStat(path), hash);
}

int main(void)
{
  struct stat stats;
  uint8_t hash[FILE_HASH_SIZE];

  testGroupStart("fileHash()");
  stats = sStat("example.txt");
  assert_error(fileHash("non-existing.txt", stats, hash),
               "failed to open \"non-existing.txt\" for reading: No such file or directory");
  assert_error(fileHash("test directory", stats, hash),
               "IO error while reading \"test directory\": Is a directory");
  assert_error(fileHash("test directory", sStat("empty.txt"), hash),
               "failed to check for remaining bytes in \"test directory\": Is a directory");

  const uint8_t empty_hash[FILE_HASH_SIZE] =
  {
    0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
    0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09,
  };
  fileHashWrapper("empty.txt", hash);
  assert_true(memcmp(hash, empty_hash, FILE_HASH_SIZE) == 0);

  const uint8_t example_hash[FILE_HASH_SIZE] =
  {
    0x10, 0x75, 0x15, 0xb9, 0x30, 0x22, 0x6d, 0x6c, 0x4a, 0x49,
    0x96, 0x66, 0x89, 0x81, 0x1c, 0xa8, 0x3f, 0x2e, 0x40, 0xa5,
  };
  fileHashWrapper("example.txt", hash);
  assert_true(memcmp(hash, example_hash, FILE_HASH_SIZE) == 0);

  fileHashWrapper("symlink.txt", hash);
  assert_true(memcmp(hash, example_hash, FILE_HASH_SIZE) == 0);

  const uint8_t bom_hash[FILE_HASH_SIZE] =
  {
    0x15, 0xc0, 0xaa, 0xe6, 0x39, 0xdb, 0x29, 0x46, 0xcb, 0x80,
    0x3a, 0x00, 0x97, 0x40, 0xb6, 0x89, 0x81, 0xd3, 0x7c, 0xd8,
  };
  fileHashWrapper("broken-config-files/BOM-simple-error.txt", hash);
  assert_true(memcmp(hash, bom_hash, FILE_HASH_SIZE) == 0);

  const uint8_t invalid_hash[FILE_HASH_SIZE] =
  {
    0x49, 0x09, 0x32, 0xa0, 0x11, 0x92, 0x26, 0x45, 0x8b, 0xff,
    0xae, 0x08, 0x2d, 0x08, 0x66, 0x9a, 0x9c, 0x95, 0x24, 0x0d,
  };
  fileHashWrapper("dummy-metadata/invalid-path-state-type", hash);
  assert_true(memcmp(hash, invalid_hash, FILE_HASH_SIZE) == 0);

  const uint8_t test_hash[FILE_HASH_SIZE] =
  {
    0xa5, 0xaf, 0xcc, 0xaa, 0x41, 0x7c, 0x7c, 0x92, 0xf5, 0x12,
    0x90, 0xce, 0xb0, 0x99, 0xf8, 0xbe, 0xa7, 0xc4, 0x7f, 0xd9,
  };
  fileHashWrapper("dummy-metadata/test-data-1", hash);
  assert_true(memcmp(hash, test_hash, FILE_HASH_SIZE) == 0);

  stats = sStat("dummy-metadata/test-data-1");
  stats.st_size += 1;
  assert_error(fileHash("dummy-metadata/test-data-1", stats, hash),
               "reading \"dummy-metadata/test-data-1\": reached end of file unexpectedly");

  stats.st_size -= 2;
  assert_error(fileHash("dummy-metadata/test-data-1", stats, hash),
               "file changed while calculating hash: \"dummy-metadata/test-data-1\"");

  stats.st_size += 1;
  stats.st_blksize = 1;
  fileHash("dummy-metadata/test-data-1", stats, hash);
  assert_true(memcmp(hash, test_hash, FILE_HASH_SIZE) == 0);

  testGroupEnd();
}
