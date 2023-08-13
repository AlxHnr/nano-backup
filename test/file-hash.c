#include "file-hash.h"

#include "safe-wrappers.h"
#include "test.h"

static void fileHashWrapper(const char *path, uint8_t *hash)
{
  fileHash(str(path), sStat(str(path)), hash);
}

int main(void)
{
  struct stat stats;
  uint8_t hash[FILE_HASH_SIZE];

  testGroupStart("fileHash()");
  stats = sStat(str("example.txt"));
  assert_error_errno(fileHash(str("non-existing.txt"), stats, hash),
                     "failed to open \"non-existing.txt\" for reading", ENOENT);
  assert_error_errno(fileHash(str("test directory"), stats, hash), "IO error while reading \"test directory\"",
                     EISDIR);
  assert_error_errno(fileHash(str("test directory"), sStat(str("empty.txt")), hash),
                     "failed to check for remaining bytes in \"test directory\"", EISDIR);

  const uint8_t empty_hash[FILE_HASH_SIZE] = {
    0x33, 0x45, 0x52, 0x4a, 0xbf, 0x6b, 0xbe, 0x18, 0x09, 0x44,
    0x92, 0x24, 0xb5, 0x97, 0x2c, 0x41, 0x79, 0x0b, 0x6c, 0xf2,
  };
  fileHashWrapper("empty.txt", hash);
  assert_true(memcmp(hash, empty_hash, FILE_HASH_SIZE) == 0);

  const uint8_t example_hash[FILE_HASH_SIZE] = {
    0x81, 0x29, 0x52, 0x03, 0x8c, 0x56, 0x80, 0x79, 0x63, 0xb3,
    0xb8, 0xbb, 0x67, 0x65, 0x28, 0x61, 0xe1, 0x46, 0x99, 0xc1,
  };
  fileHashWrapper("example.txt", hash);
  assert_true(memcmp(hash, example_hash, FILE_HASH_SIZE) == 0);

  fileHashWrapper("symlink.txt", hash);
  assert_true(memcmp(hash, example_hash, FILE_HASH_SIZE) == 0);

  const uint8_t bom_hash[FILE_HASH_SIZE] = {
    0xd6, 0x71, 0xcc, 0x28, 0xba, 0x4a, 0xfa, 0x39, 0x0d, 0x76,
    0x80, 0xb6, 0x34, 0x78, 0xc2, 0xfe, 0x0a, 0x94, 0xa5, 0xba,
  };
  fileHashWrapper("broken-config-files/BOM-simple-error.txt", hash);
  assert_true(memcmp(hash, bom_hash, FILE_HASH_SIZE) == 0);

  const uint8_t redefine_2[FILE_HASH_SIZE] = {
    0x3a, 0x83, 0x2b, 0x60, 0x59, 0x7c, 0x9f, 0x0e, 0xe2, 0x01,
    0xe2, 0x48, 0xf8, 0x21, 0xf2, 0x26, 0xbf, 0xf0, 0x46, 0xea,
  };
  fileHashWrapper("broken-config-files/redefine-2.txt", hash);
  assert_true(memcmp(hash, redefine_2, FILE_HASH_SIZE) == 0);

  const uint8_t inheritance_1[FILE_HASH_SIZE] = {
    0xd7, 0xe0, 0xbf, 0x76, 0x68, 0xfd, 0xb0, 0x00, 0x91, 0x5d,
    0x37, 0xc1, 0x35, 0x2b, 0x4d, 0x56, 0x42, 0xd1, 0x55, 0x2e,
  };
  fileHashWrapper("valid-config-files/inheritance-1.txt", hash);
  assert_true(memcmp(hash, inheritance_1, FILE_HASH_SIZE) == 0);

  stats = sStat(str("valid-config-files/inheritance-1.txt"));
  stats.st_size += 1;
  assert_error(fileHash(str("valid-config-files/inheritance-1.txt"), stats, hash),
               "reading \"valid-config-files/inheritance-1.txt\": reached end of file unexpectedly");

  stats.st_size -= 2;
  assert_error(fileHash(str("valid-config-files/inheritance-1.txt"), stats, hash),
               "file changed while calculating hash: \"valid-config-files/inheritance-1.txt\"");

  stats.st_size += 1;
  stats.st_blksize = 1;
  fileHash(str("valid-config-files/inheritance-1.txt"), stats, hash);
  assert_true(memcmp(hash, inheritance_1, FILE_HASH_SIZE) == 0);

  testGroupEnd();
}
