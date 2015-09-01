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

/** An union used for determining the current systems endianness. */
static const union
{
  uint32_t value;
  uint8_t array[4];

  /* Assert that the size of time_t is either 4 or 8. */
  char assert[(sizeof(time_t) == 4) || (sizeof(time_t) == 8)];
}endian_test = { .value = 1 };

/** Assert that enough unread bytes are remaining in the given file
  content.

  @param reader_position The position of the reader.
  @param bytes The amount of bytes which should be unread.
  @param content The content of a file.
  @param metadata_path The path to the metadata file, for printing error
  messages.
*/
static void assertBytesLeft(size_t reader_position, size_t bytes,
                            FileContent content, const char *metadata_path)
{
  if(sSizeAdd(reader_position, bytes) > content.size)
  {
    die("broken metadata file: \"%s\"", metadata_path);
  }
}

/** Converts the endianness of the given value. If the current system is
  little endian, no conversion will happen. Otherwise the bytes will be
  swapped.

  @param value The value that should be converted.

  @return The converted value.
*/
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

/** The 64 bit version of convertEndian32(). */
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

/** Reads a byte from the given FileContent.

  @param content The content of the file from which should be read.
  @param reader_position The position of the reader, which will be moved to
  the next unread byte.
  @param metadata_path The path to the metadata file, for printing error
  messages.

  @return The current byte at the given reader position in the given
  FileContent.
*/
static uint8_t read8(FileContent content, size_t *reader_position,
                     const char *metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint8_t),
                  content, metadata_path);

  uint8_t byte = content.content[*reader_position];
  *reader_position += sizeof(byte);

  return byte;
}

/** The 4 byte version of read8() which takes care of endian conversion. */
static uint32_t read32(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint32_t),
                  content, metadata_path);

  uint32_t size = *((uint32_t *)&content.content[*reader_position]);
  *reader_position += sizeof(size);

  return convertEndian32(size);
}

/** The 8 byte version of read8() which takes care of endian conversion. */
static uint64_t read64(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint64_t),
                  content, metadata_path);

  uint64_t size = *((uint64_t *)&content.content[*reader_position]);
  *reader_position += sizeof(size);

  return convertEndian64(size);
}

/** A wrapper around read64(), which ensures that the read value fits into
  size_t. */
static size_t readSize(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  uint64_t size = read64(content, reader_position, metadata_path);

  if(size > SIZE_MAX)
  {
    die("failed to read 64 bit size value from \"%s\"", metadata_path);
  }

  return (size_t)size;
}

/** A wrapper around read64(), which ensures that the read value fits into
  time_t. */
static time_t readTime(FileContent content, size_t *reader_position,
                       const char *metadata_path)
{
  uint64_t time = read64(content, reader_position, metadata_path);

  if((sizeof(time_t) == 8 && time > INT64_MAX) ||
     (sizeof(time_t) == 4 && time > INT32_MAX))
  {
    die("overflow reading timestamp from \"%s\"", metadata_path);
  }

  return (time_t)time;
}

/** Reads the hash from a files content.

  @param content The content of the file containing the hash.
  @param reader_position The reader position at which the hash starts. It
  will be moved to the next unread byte.
  @param metadata_path The path to the file of the content, for printing
  the error message.
  @param hash The address of the array, into which the hash will be copied.
*/
static void readHash(FileContent content, size_t *reader_position,
                     const char *metadata_path, uint8_t *hash)
{
  assertBytesLeft(*reader_position, SHA_DIGEST_LENGTH,
                  content, metadata_path);

  memcpy(hash, &content.content[*reader_position], SHA_DIGEST_LENGTH);
  *reader_position += SHA_DIGEST_LENGTH;
}

/** Reads a PathHistory struct from the content of the given file.

  @param content The content containing the PathHistory.
  @param reader_position The position of the path history. It will be moved
  to the next unread byte.
  @param metadata_path The path to the file to which the given content
  belongs to.
  @param backup_history An array containing the backup history.

  @return A new path history allocated inside the internal memory pool,
  which should not be freed by the caller.
*/
static PathHistory *readPathHistory(FileContent content,
                                    size_t *reader_position,
                                    const char *metadata_path,
                                    Backup *backup_history)
{
  PathHistory *point = mpAlloc(sizeof *point);

  size_t id = readSize(content, reader_position, metadata_path);
  point->backup = &backup_history[id];

  point->state.type = read8(content, reader_position, metadata_path);

  point->state.uid = read32(content, reader_position, metadata_path);
  point->state.gid = read32(content, reader_position, metadata_path);
  point->state.timestamp =
    readTime(content, reader_position, metadata_path);

  if(point->state.type == PST_regular)
  {
    point->state.metadata.reg.mode =
      read32(content, reader_position, metadata_path);

    point->state.metadata.reg.size =
      readSize(content, reader_position, metadata_path);

    readHash(content, reader_position, metadata_path,
             point->state.metadata.reg.hash);
  }
  else if(point->state.type == PST_symlink)
  {
    size_t target_length =
      readSize(content, reader_position, metadata_path);

    assertBytesLeft(*reader_position, target_length,
                    content, metadata_path);

    char *target = mpAlloc(sSizeAdd(target_length, 1));

    memcpy(target, &content.content[*reader_position], target_length);
    *reader_position += target_length;

    target[target_length] = '\0';

    point->state.metadata.sym_target = target;
  }
  else if(point->state.type == PST_directory)
  {
    point->state.metadata.dir_mode =
      read32(content, reader_position, metadata_path);
  }
  else if(point->state.type != PST_non_existing)
  {
    die("invalid PathStateType in \"%s\"", metadata_path);
  }

  point->next = NULL;
  return point;
}

/** Reads a full path history from the given files content.

  @param content The content containing the path history.
  @param reader_position The position from which should be read. It will be
  moved to the next unread byte.
  @param metadata_path The path to the metadata file.
  @param backup_history An array containing the backup history.

  @return A complete path history allocated inside the internal memory
  pool. It should not be freed by the caller.
*/
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

/** Reads the subnodes of the given parent node recursively.

  @param content The content of the file from which the subnodes should be
  read.
  @param reader_position The position of the reader at which the subnodes
  start. It will be moved to the next unread byte once this function
  completes.
  @param metadata_path The path to the metadata file. Only needed to print
  error messages.
  @param parent_node The parent node to which the read subnodes belong to.
  It will not be modified. If the parent node does not exist, NULL can be
  passed instead.
  @param metadata The metadata of the repository to which the nodes belong
  to. Its path table will be used for mapping full paths to nodes.

  @return A node list allocated inside the internal memory pool, which
  should not be freed by the caller. It can be NULL, if the given parent
  node has no subnodes.
*/
static PathNode *readPathSubnodes(FileContent content,
                                  size_t *reader_position,
                                  const char *metadata_path,
                                  PathNode *parent_node,
                                  Metadata *metadata)
{
  size_t node_count = readSize(content, reader_position, metadata_path);
  PathNode *node_tree = NULL;

  for(size_t counter = 0; counter < node_count; counter++)
  {
    PathNode *node = mpAlloc(sizeof *node);

    /* Prepend current node to node tree. */
    node->next = node_tree;
    node_tree = node;

    /* Read the name and append it to the parent nodes path. */
    size_t name_length = readSize(content, reader_position, metadata_path);
    assertBytesLeft(*reader_position, name_length, content, metadata_path);

    String name =
      (String)
      {
        .str = &content.content[*reader_position],
        .length = name_length
      };
    *reader_position += name_length;

    String full_path = strAppendPath(parent_node == NULL? str(""):
                                     parent_node->path, name);

    memcpy(&node->path, &full_path, sizeof(node->path));

    /* Read other node variables. */
    strtableMap(metadata->path_table, node->path, node);

    node->policy = read8(content, reader_position, metadata_path);
    node->history =
      readFullPathHistory(content, reader_position, metadata_path,
                          metadata->backup_history);

    node->subnodes = readPathSubnodes(content, reader_position,
                                      metadata_path, node, metadata);
  }

  return node_tree;
}

/** Loads the metadata of a repository.

  @param path The full or relative path to the metadata file.

  @return The metadata, allocated inside the internal memory pool, which
  should not be freed by the caller.
*/
Metadata *loadMetadata(const char *path)
{
  FileContent content = sGetFilesContent(path);

  /* Allocate and initialize metadata. */
  Metadata *metadata = mpAlloc(sizeof *metadata);

  metadata->current_backup.id = 0;
  metadata->current_backup.timestamp = 0;
  metadata->current_backup.ref_count = 0;

  /* Read backup history. */
  size_t reader_position = 0;

  metadata->backup_history_length =
    readSize(content, &reader_position, path);

  metadata->backup_history =
    mpAlloc(sSizeMul(sizeof *metadata->backup_history,
                     metadata->backup_history_length));

  for(size_t id = 0; id < metadata->backup_history_length; id++)
  {
    metadata->backup_history[id].id = id;

    metadata->backup_history[id].timestamp =
      readTime(content, &reader_position, path);

    metadata->backup_history[id].ref_count =
      readSize(content, &reader_position, path);
  }

  metadata->config_history =
    readFullPathHistory(content, &reader_position,
                        path, metadata->backup_history);

  metadata->total_path_count = readSize(content, &reader_position, path);

  metadata->path_table = strtableNewFixed(metadata->total_path_count);
  metadata->paths = readPathSubnodes(content, &reader_position,
                                     path, NULL, metadata);

  if(reader_position != content.size)
  {
    die("inconsistent byte count in \"%s\"", path);
  }

  free(content.content);
  return metadata;
}
