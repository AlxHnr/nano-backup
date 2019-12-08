/** @file
  Implements functions to handle the metadata of a repository.
*/

#include "metadata.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "CRegion/global-region.h"
#include "CRegion/static-assert.h"

#include "file-hash.h"
#include "memory-pool.h"
#include "safe-math.h"
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
                            FileContent content, String metadata_path)
{
  if(sSizeAdd(reader_position, bytes) > content.size)
  {
    die("corrupted metadata: expected %zu byte%s, got %zu: \"%s\"", bytes,
        bytes == 1? "":"s", content.size - reader_position, metadata_path.content);
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
#ifndef __GNUC__
    return 0;
#endif
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
#ifndef __GNUC__
    return 0;
#endif
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
                     String metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint8_t),
                  content, metadata_path);

  uint8_t byte = content.content[*reader_position];
  *reader_position += sizeof(byte);

  return byte;
}

/** Counterpart to read8().

  @param value The value which should be written.
  @param writer The RepoWriter to be used for writing.
*/
static void write8(uint8_t value, RepoWriter *writer)
{
  repoWriterWrite(&value, sizeof(value), writer);
}

/** The 4 byte version of read8() which takes care of endian conversion. */
static uint32_t read32(FileContent content, size_t *reader_position,
                       String metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint32_t),
                  content, metadata_path);

  uint32_t value;
  memcpy(&value, &content.content[*reader_position], sizeof(value));
  *reader_position += sizeof(value);

  return convertEndian32(value);
}

/** The 4 byte version of write8() which takes care of endian conversion. */
static void write32(uint32_t value, RepoWriter *writer)
{
  uint32_t converted_value = convertEndian32(value);
  repoWriterWrite(&converted_value, sizeof(converted_value), writer);
}

/** The 8 byte version of read8() which takes care of endian conversion. */
static uint64_t read64(FileContent content, size_t *reader_position,
                       String metadata_path)
{
  assertBytesLeft(*reader_position, sizeof(uint64_t),
                  content, metadata_path);

  uint64_t value;
  memcpy(&value, &content.content[*reader_position], sizeof(value));
  *reader_position += sizeof(value);

  return convertEndian64(value);
}

/** The 8 byte version of write8() which takes care of endian conversion. */
static void write64(uint64_t value, RepoWriter *writer)
{
  uint64_t converted_value = convertEndian64(value);
  repoWriterWrite(&converted_value, sizeof(converted_value), writer);
}

/** A wrapper around read64(), which ensures that the read value fits into
  size_t. */
static size_t readSize(FileContent content, size_t *reader_position,
                       String metadata_path)
{
  uint64_t size = read64(content, reader_position, metadata_path);

  if(size > SIZE_MAX)
  {
    die("failed to read 64 bit size value from \"%s\"", metadata_path.content);
  }

  return (size_t)size;
}

/** A wrapper around read64(), which ensures that the read value fits into
  time_t. */
static time_t readTime(FileContent content, size_t *reader_position,
                       String metadata_path)
{
  CR_StaticAssert(sizeof(time_t) == 4 || sizeof(time_t) == 8);

  int64_t time = read64(content, reader_position, metadata_path);

  if(sizeof(time_t) == 4 && (time < INT32_MIN || time > INT32_MAX))
  {
    die("unable to read 64-bit timestamp from \"%s\"", metadata_path.content);
  }

  return (time_t)time;
}

/** Reads bytes into a specified buffer.

  @param content The FileContent struct from which the bytes should be
  read.
  @param reader_position The position of the first byte to read in the
  given content. It will be moved to the next unread byte once this
  function returns.
  @param buffer The buffer in which the bytes should be stored.
  @param size The amount of bytes to be read.
  @param metadata_path The path to the metadata file, for printing useful
  error messages.
*/
static void readBytes(FileContent content, size_t *reader_position,
                      uint8_t *buffer, size_t size, String metadata_path)
{
  assertBytesLeft(*reader_position, size, content, metadata_path);

  memcpy(buffer, &content.content[*reader_position], size);
  *reader_position += size;
}

/** Reads a PathHistory struct from the content of the given file.

  @param content The content containing the PathHistory.
  @param reader_position The position of the path history. It will be moved
  to the next unread byte.
  @param metadata_path The path to the file to which the given content
  belongs to.
  @param metadata The metadata to which the returned PathHistory belongs.

  @return A new path history allocated inside the internal memory pool,
  which should not be freed by the caller.
*/
static PathHistory *readPathHistory(FileContent content,
                                    size_t *reader_position,
                                    String metadata_path,
                                    Metadata *metadata)
{
  PathHistory *point = mpAlloc(sizeof *point);

  size_t id = readSize(content, reader_position, metadata_path);
  if(id >= metadata->backup_history_length)
  {
    die("backup id is out of range in \"%s\"", metadata_path.content);
  }

  point->backup = &metadata->backup_history[id];
  point->backup->ref_count = sSizeAdd(point->backup->ref_count, 1);

  point->state.type = read8(content, reader_position, metadata_path);

  if(point->state.type != PST_non_existing)
  {
    point->state.uid = read32(content, reader_position, metadata_path);
    point->state.gid = read32(content, reader_position, metadata_path);
  }

  if(point->state.type == PST_regular)
  {
    point->state.metadata.reg.mode =
      read32(content, reader_position, metadata_path);

    point->state.metadata.reg.timestamp =
      readTime(content, reader_position, metadata_path);

    point->state.metadata.reg.size =
      read64(content, reader_position, metadata_path);

    if(point->state.metadata.reg.size > FILE_HASH_SIZE)
    {
      readBytes(content, reader_position,
                point->state.metadata.reg.hash,
                FILE_HASH_SIZE, metadata_path);
      point->state.metadata.reg.slot =
        read8(content, reader_position, metadata_path);
    }
    else if(point->state.metadata.reg.size > 0)
    {
      readBytes(content, reader_position,
                point->state.metadata.reg.hash,
                point->state.metadata.reg.size, metadata_path);
    }
  }
  else if(point->state.type == PST_symlink)
  {
    size_t target_length =
      readSize(content, reader_position, metadata_path);

    char *buffer = mpAlloc(sSizeAdd(target_length, 1));

    readBytes(content, reader_position, (uint8_t *)buffer, target_length,
              metadata_path);

    buffer[target_length] = '\0';

    strSet(&point->state.metadata.sym_target, strWrap(buffer));
  }
  else if(point->state.type == PST_directory)
  {
    point->state.metadata.dir.mode =
      read32(content, reader_position, metadata_path);
    point->state.metadata.dir.timestamp =
      readTime(content, reader_position, metadata_path);
  }
  else if(point->state.type != PST_non_existing)
  {
    die("invalid PathStateType in \"%s\"", metadata_path.content);
  }

  point->next = NULL;
  return point;
}

/** Reads a full path history from the given files content.

  @param content The content containing the path history.
  @param reader_position The position from which should be read. It will be
  moved to the next unread byte.
  @param metadata_path The path to the metadata file.
  @param metadata The metadata to which the returned PathHistory belongs.

  @return A complete path history allocated inside the internal memory
  pool. It should not be freed by the caller.
*/
static PathHistory *readFullPathHistory(FileContent content,
                                        size_t *reader_position,
                                        String metadata_path,
                                        Metadata *metadata)
{
  size_t history_length =
    readSize(content, reader_position, metadata_path);

  if(history_length == 0)
  {
    return NULL;
  }

  PathHistory *first_point = readPathHistory(content, reader_position,
                                             metadata_path, metadata);
  PathHistory *current_point = first_point;

  for(size_t counter = 1; counter < history_length; counter++)
  {
    current_point->next = readPathHistory(content, reader_position,
                                          metadata_path, metadata);
    current_point = current_point->next;
  }

  return first_point;
}

/** Writes the given history list via the specified RepoWriter.
  Counterpart to readFullPathHistory().

  @param starting_point The first element in the list.
  @param writer The writer which should be used for writing.
*/
static void writePathHistoryList(PathHistory *starting_point,
                                 RepoWriter *writer)
{
  size_t history_length = 0;
  for(PathHistory *point = starting_point;
      point != NULL; point = point->next)
  {
    history_length = sSizeAdd(history_length, 1);
  }

  write64(history_length, writer);
  for(PathHistory *point = starting_point;
      point != NULL; point = point->next)
  {
    write64(point->backup->id, writer);
    write8(point->state.type,  writer);

    if(point->state.type != PST_non_existing)
    {
      write32(point->state.uid,  writer);
      write32(point->state.gid,  writer);
    }

    if(point->state.type == PST_regular)
    {
      write32(point->state.metadata.reg.mode, writer);
      write64(point->state.metadata.reg.timestamp, writer);
      write64(point->state.metadata.reg.size, writer);

      if(point->state.metadata.reg.size > FILE_HASH_SIZE)
      {
        repoWriterWrite(point->state.metadata.reg.hash,
                             FILE_HASH_SIZE, writer);
        write8(point->state.metadata.reg.slot, writer);
      }
      else if(point->state.metadata.reg.size > 0)
      {
        repoWriterWrite(point->state.metadata.reg.hash,
                        point->state.metadata.reg.size, writer);
      }
    }
    else if(point->state.type == PST_symlink)
    {
      String target_path = point->state.metadata.sym_target;
      write64(target_path.length, writer);
      repoWriterWrite(target_path.content, target_path.length, writer);
    }
    else if(point->state.type == PST_directory)
    {
      write32(point->state.metadata.dir.mode, writer);
      write64(point->state.metadata.dir.timestamp, writer);
    }
  }
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
                                  String metadata_path,
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
    if(name_length == 0)
    {
      die("contains filename with length zero: \"%s\"", metadata_path.content);
    }

    assertBytesLeft(*reader_position, name_length, content, metadata_path);

    String name =
      strSlice(&content.content[*reader_position], name_length);
    *reader_position += name_length;

    if(memchr(name.content, '\0', name.length) != NULL)
    {
      die("contains filename with null-bytes: \"%s\"", metadata_path.content);
    }
    else if(memchr(name.content, '/', name.length) != NULL ||
            strIsDotElement(name))
    {
      die("contains invalid filename \"%s\": \"%s\"",
          strCopy(name).content, metadata_path.content);
    }

    String full_path = strAppendPath(parent_node == NULL? strWrap(""):
                                     parent_node->path, name);

    memcpy(&node->path, &full_path, sizeof(node->path));

    /* Read other node variables. */
    strTableMap(metadata->path_table, node->path, node);

    node->hint = BH_none;
    node->policy = read8(content, reader_position, metadata_path);
    node->history = readFullPathHistory(content, reader_position,
                                        metadata_path, metadata);

    node->subnodes = readPathSubnodes(content, reader_position,
                                      metadata_path, node, metadata);
  }

  return node_tree;
}

/** Writes the given list of path nodes recursively.

  @param node_list The list, which should be written.
  @param writer The writer, which should be used for writing.
*/
static void writePathList(PathNode *node_list, RepoWriter *writer)
{
  size_t list_length = 0;
  for(PathNode *node = node_list; node != NULL; node = node->next)
  {
    if(backupHintNoPol(node->hint) != BH_not_part_of_repository)
    {
      list_length = sSizeAdd(list_length, 1);
    }
  }

  write64(list_length, writer);

  for(PathNode *node = node_list; node != NULL; node = node->next)
  {
    if(backupHintNoPol(node->hint) != BH_not_part_of_repository)
    {
      String name = strSplitPath(node->path).tail;
      write64(name.length, writer);
      repoWriterWrite(name.content, name.length, writer);

      write8(node->policy, writer);
      writePathHistoryList(node->history, writer);
      writePathList(node->subnodes, writer);
    }
  }
}

/** Creates an empty metadata struct.

  @return A metadata struct which should not be freed by the caller.
*/
Metadata *metadataNew(void)
{
  Metadata *metadata = mpAlloc(sizeof *metadata);

  metadata->current_backup.id = 0;
  metadata->current_backup.timestamp = 0;
  metadata->current_backup.ref_count = 0;

  metadata->backup_history_length = 0;
  metadata->backup_history = NULL;

  metadata->config_history = NULL;

  metadata->total_path_count = 0;
  metadata->path_table = strTableNew(CR_GetGlobalRegion());
  metadata->paths = NULL;

  return metadata;
}

/** Loads the metadata of a repository.

  @param path The full or relative path to the metadata file.

  @return The metadata, allocated inside the internal memory pool, which
  should not be freed by the caller.
*/
Metadata *metadataLoad(String path)
{
  CR_Region *content_region = CR_RegionNew();
  FileContent content = sGetFilesContent(content_region, path);

  /* Allocate and initialize metadata. */
  Metadata *metadata = mpAlloc(sizeof *metadata);

  metadata->current_backup.id = 0;
  metadata->current_backup.timestamp = 0;
  metadata->current_backup.ref_count = 0;

  /* Read backup history. */
  size_t reader_position = 0;

  metadata->backup_history_length =
    readSize(content, &reader_position, path);

  if(metadata->backup_history_length == 0)
  {
    metadata->backup_history = NULL;
  }
  else
  {
    metadata->backup_history =
      mpAlloc(sSizeMul(sizeof *metadata->backup_history,
                       metadata->backup_history_length));
  }

  for(size_t id = 0; id < metadata->backup_history_length; id++)
  {
    metadata->backup_history[id].id = id;

    metadata->backup_history[id].timestamp =
      readTime(content, &reader_position, path);

    metadata->backup_history[id].ref_count = 0;
  }

  metadata->config_history =
    readFullPathHistory(content, &reader_position, path, metadata);

  metadata->total_path_count = readSize(content, &reader_position, path);
  metadata->path_table = strTableNew(CR_GetGlobalRegion());

  metadata->paths = readPathSubnodes(content, &reader_position,
                                     path, NULL, metadata);
  CR_RegionRelease(content_region);

  if(reader_position != content.size)
  {
    die("unneeded trailing bytes in \"%s\"", path.content);
  }

  return metadata;
}

/** Writes the given metadata into the specified repositories metadata
  file. Counterpart to loadMetadata(). This function writes only referenced
  history points and will modify their backup IDs.

  @param metadata The metadata that should be written.
  @param repo_path The full or relative path to the repository, which
  should contain the metadata.
  @param repo_tmp_file_path The path to the repositories temporary file.
  @param repo_metadata_path The path to the repositories metadata file.
*/
void metadataWrite(Metadata *metadata,
                   String repo_path,
                   String repo_tmp_file_path,
                   String repo_metadata_path)
{
  RepoWriter *writer = repoWriterOpenRaw(repo_path, repo_tmp_file_path,
                                         strWrap("metadata"), repo_metadata_path);

  /* Count referenced history points and update IDs. */
  size_t id_counter = metadata->current_backup.ref_count > 0;
  for(size_t index = 0; index < metadata->backup_history_length; index++)
  {
    if(metadata->backup_history[index].ref_count > 0)
    {
      metadata->backup_history[index].id = id_counter;
      id_counter = sSizeAdd(id_counter, 1);
    }
  }

  /* Write the backup history. */
  write64(id_counter, writer);

  if(metadata->current_backup.ref_count > 0)
  {
    write64(metadata->current_backup.timestamp, writer);
  }

  for(size_t index = 0; index < metadata->backup_history_length; index++)
  {
    Backup *backup = &metadata->backup_history[index];
    if(backup->ref_count > 0)
    {
      write64(backup->timestamp, writer);
    }
  }

  /* Write the config files history. */
  writePathHistoryList(metadata->config_history, writer);

  /* Write the path tree. */
  write64(metadata->total_path_count, writer);
  writePathList(metadata->paths, writer);

  /* Finish writing. */
  repoWriterClose(writer);
}
