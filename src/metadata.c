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
  Implements functions to handle the metadata of a repository.
*/

#include "metadata.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "memory-pool.h"
#include "safe-wrappers.h"
#include "error-handling.h"

#if CHAR_BIT != 8
#error CHAR_BIT must be 8
#endif

#if SIZE_MAX > UINT64_MAX
#error system can address more than 64 bits
#endif

static const union
{
  uint32_t value;
  uint8_t array[4];
}endian_test = { .value = 1 };

static void assertBytesLeft(size_t reader_position, size_t bytes,
                            FileContent content, const char *metadata_path)
{
  if(sSizeAdd(reader_position, bytes) > content.size)
  {
    die("broken metadata file: \"%s\"", metadata_path);
  }
}

static uint32_t convertEndian32(uint32_t value)
{
  if(endian_test.array[0] == 1)
  {
    return value;
  }
  else if(endian_test.array[3] == 1)
  {
    value = ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0xFF00FF);
    return (value << 16) | (value >> 16);
  }
  else
  {
    die("failed to convert value: Unsupported endianness");
    return 0;
  }
}

static uint64_t convertEndian64(uint64_t value)
{
  if(endian_test.array[0] == 1)
  {
    return value;
  }
  else if(endian_test.array[3] == 1)
  {
    value = ((value << 8) & 0xFF00FF00FF00FF00ULL) |
            ((value >> 8) & 0x00FF00FF00FF00FFULL);
    value = ((value << 16) & 0xFFFF0000FFFF0000ULL) |
            ((value >> 16) & 0x0000FFFF0000FFFFULL);
    return (value << 32) | (value >> 32);
  }
  else
  {
    die("failed to convert value: Unsupported endianness");
    return 0;
  }
}

static uint8_t read8(FileContent content, size_t *reader_position,
                     const char *metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint8_t),
                  content, metadata_path);

  uint8_t byte = content.content[*reader_position];
  *reader_position += sizeof(byte);

  return byte;
}

static uint32_t read32(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint32_t),
                  content, metadata_path);

  uint32_t size = (uint32_t)content.content[*reader_position];
  *reader_position += sizeof(size);

  return convertEndian32(size);
}

static uint64_t read64(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint64_t),
                  content, metadata_path);

  uint64_t size = (uint64_t)content.content[*reader_position];
  *reader_position += sizeof(size);

  return convertEndian64(size);
}

static size_t readSize(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  uint64_t size = read64(content, reader_position, metadata_path);

  /** Assert that a 64 bit value fits into size_t. */
  if(size > SIZE_MAX)
  {
    die("failed to read 64 bit integer from \"%s\"", metadata_path);
  }

  return (size_t)size;
}

static time_t readTime(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  uint64_t time = read64(content, reader_position, metadata_path);

  if((sizeof(time_t) == 8 && time > INT64_MAX) ||
     (sizeof(time_t) == 4 && time > INT32_MAX))
  {
    die("failed to read timestamp from \"%s\"", metadata_path);
  }

  return (time_t)time;
}

static void readHash(FileContent content, size_t *reader_position,
                     const char *metadata_path, uint8_t *hash)
{
  assertBytesLeft(*reader_position, SHA_DIGEST_LENGTH,
                  content, metadata_path);

  memcpy(hash, &content.content[*reader_position], SHA_DIGEST_LENGTH);
  *reader_position += SHA_DIGEST_LENGTH;
}

static PathHistory *readPathHistory(FileContent content,
                                    size_t *reader_position,
                                    const char *metadata_path,
                                    Backup *backup_history)
{
  PathHistory *point = mpAlloc(sizeof *point);

  size_t id = readSize(content, reader_position, metadata_path);
  point->backup = &backup_history[id];

  point->state.type = read8(content, reader_position, metadata_path);

  if(point->state.type == PST_regular)
  {
    point->state.metadata.reg.uid =
      read32(content, reader_position, metadata_path);

    point->state.metadata.reg.gid =
      read32(content, reader_position, metadata_path);

    point->state.metadata.reg.timestamp =
      readTime(content, reader_position, metadata_path);

    point->state.metadata.reg.mode =
      read32(content, reader_position, metadata_path);

    point->state.metadata.reg.size =
      readSize(content, reader_position, metadata_path);

    readHash(content, reader_position, metadata_path,
             point->state.metadata.reg.hash);
  }
  else if(point->state.type == PST_symlink)
  {
    point->state.metadata.sym.uid =
      read32(content, reader_position, metadata_path);

    point->state.metadata.sym.gid =
      read32(content, reader_position, metadata_path);

    point->state.metadata.sym.timestamp =
      readTime(content, reader_position, metadata_path);

    size_t target_length =
      readSize(content, reader_position, metadata_path);

    assertBytesLeft(*reader_position, target_length,
                    content, metadata_path);

    char *target = mpAlloc(sSizeAdd(target_length, 1));

    memcpy(target, &content.content[*reader_position], target_length);
    *reader_position += target_length;

    target[target_length] = '\0';

    point->state.metadata.sym.target = target;
  }
  else if(point->state.type == PST_directory)
  {
    point->state.metadata.dir.uid =
      read32(content, reader_position, metadata_path);

    point->state.metadata.dir.gid =
      read32(content, reader_position, metadata_path);

    point->state.metadata.dir.timestamp =
      readTime(content, reader_position, metadata_path);

    point->state.metadata.dir.mode =
      read32(content, reader_position, metadata_path);
  }
  else if(point->state.type != PST_non_existing)
  {
    die("invalid PathStateType in \"%s\"", metadata_path);
  }

  point->next = NULL;
  return point;
}

static PathHistory *readFullPathHistory(FileContent content,
                                        size_t *reader_position,
                                        const char *metadata_path,
                                        Backup *backup_history)
{
  size_t history_length =
    readSize(content, reader_position, metadata_path);

  if(history_length == 0)
  {
    return NULL;
  }

  PathHistory *first_point = readPathHistory(content, reader_position,
                                             metadata_path, backup_history);
  PathHistory *current_point = first_point;

  for(size_t counter = 1; counter < history_length; counter++)
  {
    current_point->next = readPathHistory(content, reader_position,
                                          metadata_path, backup_history);
    current_point = current_point->next;
  }

  return first_point;
}

Metadata *loadRepoMetadata(String repo_path)
{
  String metadata_path = strAppendPath(repo_path, str("metadata"));
  FileContent content = sGetFilesContent(metadata_path.str);

  /* Allocate and initialize metadata. */
  Metadata *metadata = mpAlloc(sizeof *metadata);
  memcpy(&metadata->repo_path, &repo_path, sizeof(metadata->repo_path));

  metadata->current_backup.id = 0;
  metadata->current_backup.timestamp = 0;
  metadata->current_backup.ref_count = 0;

  /* Read backup history. */
  size_t reader_position = 0;

  metadata->backup_history_length =
    readSize(content, &reader_position, metadata_path.str);

  metadata->backup_history =
    mpAlloc(sSizeMul(sizeof *metadata->backup_history,
                     metadata->backup_history_length));

  for(size_t id = 0; id < metadata->backup_history_length; id++)
  {
    metadata->backup_history[id].id = id;

    metadata->backup_history[id].timestamp =
      readTime(content, &reader_position, metadata_path.str);

    metadata->backup_history[id].ref_count =
      readSize(content, &reader_position, metadata_path.str);
  }

  metadata->config_history =
    readFullPathHistory(content, &reader_position,
                        metadata_path.str, metadata->backup_history);

  metadata->total_path_count =
    readSize(content, &reader_position, metadata_path.str);

  metadata->path_table = strtableNewFixed(metadata->total_path_count);

  free(content.content);
  return metadata;
}
