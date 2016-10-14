/*
  Copyright (c) 2016 Alexander Heinrich

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
  Implements various helper functions for doing backup related things.
*/

#include "backup-helpers.h"

#include <string.h>
#include <unistd.h>

#include "safe-wrappers.h"
#include "error-handling.h"

static Buffer *io_buffer = NULL;

/** Checks if the content of a regular file has changed.

  @param node A node representing a file with a size greater than 0 at its
  current history point. Its size should not have changed since the last
  backup. This function will update the node if the file has changed.
  @param state The path state to check and update.
  @param stats The stats of the file represented by the given node.
*/
static void checkFileContentChanges(PathNode *node, PathState *state,
                                    struct stat stats)
{
  uint8_t hash[FILE_HASH_SIZE];
  size_t bytes_used = FILE_HASH_SIZE;

  if(state->metadata.reg.size > FILE_HASH_SIZE)
  {
    fileHash(node->path.str, stats, hash);
  }
  else
  {
    bytes_used = state->metadata.reg.size;

    FileStream *stream = sFopenRead(node->path.str);
    sFread(hash, bytes_used, stream);
    bool stream_not_at_end = sFbytesLeft(stream);
    sFclose(stream);

    if(stream_not_at_end)
    {
      die("file has changed while checking for changes: \"%s\"",
          node->path.str);
    }
  }

  if(memcmp(state->metadata.reg.hash, hash, bytes_used) != 0)
  {
    backupHintSet(node->hint, BH_content_changed);
    backupHintSet(node->hint, BH_fresh_hash);

    memcpy(state->metadata.reg.hash, hash, bytes_used);
  }
}

/** Reads the content of a symlink into the given Buffer.

  @param path The path to the symlink.
  @param stats The stats of the symlink.
  @param buffer_ptr The Buffer in which the string will be stored.

  @return The given buffers data.
*/
const char *readSymlink(const char *path, struct stat stats,
                        Buffer **buffer_ptr)
{
  uint64_t buffer_length = sUint64Add(stats.st_size, 1);
  if(buffer_length > SIZE_MAX)
  {
    die("symlink does not fit in memory: \"%s\"", path);
  }

  bufferEnsureCapacity(buffer_ptr, buffer_length);
  char *buffer = (*buffer_ptr)->data;

  /* Although st_size bytes are enough to store the symlinks target path,
     the full buffer is used. This allows to detect whether the symlink
     has increased in size since its last lstat() or not. */
  ssize_t read_bytes = readlink(path, buffer, buffer_length);

  if(read_bytes == -1)
  {
    dieErrno("failed to read symlink: \"%s\"", path);
  }
  else if(read_bytes != stats.st_size)
  {
    die("symlink changed while reading: \"%s\"", path);
  }

  buffer[stats.st_size] = '\0';

  return buffer;
}

/** Compares the node against the stats in the given results and updates
  both its backup hint and the specified path state.

  @param node The node containing the hint to update.
  @param state The state to update.
  @param stats The stats of the file represented by the given node.
*/
void applyNodeChanges(PathNode *node, PathState *state, struct stat stats)
{
  if(state->uid != stats.st_uid ||
     state->gid != stats.st_gid)
  {
    backupHintSet(node->hint, BH_owner_changed);
    state->uid = stats.st_uid;
    state->gid = stats.st_gid;
  }

  /* Path state specific change checks. */
  if(state->type == PST_regular)
  {
    if(state->metadata.reg.mode != stats.st_mode)
    {
      backupHintSet(node->hint, BH_permissions_changed);
      state->metadata.reg.mode = stats.st_mode;
    }

    if(state->metadata.reg.timestamp != stats.st_mtime)
    {
      backupHintSet(node->hint, BH_timestamp_changed);
      state->metadata.reg.timestamp = stats.st_mtime;
    }

    if(state->metadata.reg.size != (uint64_t)stats.st_size)
    {
      backupHintSet(node->hint, BH_content_changed);
      state->metadata.reg.size = stats.st_size;
    }
    else if((node->hint & BH_timestamp_changed) &&
            state->metadata.reg.size > 0)
    {
      checkFileContentChanges(node, state, stats);
    }
  }
  else if(state->type == PST_symlink)
  {
    const char *sym_target =
      readSymlink(node->path.str, stats, &io_buffer);

    if(strcmp(state->metadata.sym_target, sym_target) != 0)
    {
      state->metadata.sym_target = strCopy(str(sym_target)).str;
      backupHintSet(node->hint, BH_content_changed);
    }
  }
  else if(state->type == PST_directory)
  {
    if(state->metadata.dir.mode != stats.st_mode)
    {
      backupHintSet(node->hint, BH_permissions_changed);
      state->metadata.dir.mode = stats.st_mode;
    }

    if(state->metadata.dir.timestamp != stats.st_mtime)
    {
      backupHintSet(node->hint, BH_timestamp_changed);
      state->metadata.dir.timestamp = stats.st_mtime;
    }
  }
}
