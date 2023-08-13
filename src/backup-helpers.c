#include "backup-helpers.h"

#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "CRegion/alloc-growable.h"

#include "error-handling.h"
#include "safe-math.h"
#include "safe-wrappers.h"

/** Checks if the content of a regular file has changed.

  @param node A node representing a file with a size greater than 0 at its
  current history point. Its size should not have changed since the last
  backup. This function will update the node if the file has changed.
  @param state The path state to check and update.
  @param stats The stats of the file represented by the given node.
*/
static void checkFileContentChanges(PathNode *node, PathState *state,
                                    const struct stat stats)
{
  uint8_t hash[FILE_HASH_SIZE];
  size_t bytes_used = FILE_HASH_SIZE;

  if(state->metadata.file_info.size > FILE_HASH_SIZE)
  {
    fileHash(node->path, stats, hash);
  }
  else
  {
    bytes_used = state->metadata.file_info.size;

    FileStream *stream = sFopenRead(node->path);
    sFread(hash, bytes_used, stream);
    bool stream_not_at_end = sFbytesLeft(stream);
    sFclose(stream);

    if(stream_not_at_end)
    {
      die("file has changed while checking for changes: \"%s\"",
          node->path.content);
    }
  }

  if(memcmp(state->metadata.file_info.hash, hash, bytes_used) != 0)
  {
    backupHintSet(node->hint, BH_content_changed);
    backupHintSet(node->hint, BH_fresh_hash);

    memcpy(state->metadata.file_info.hash, hash, bytes_used);
  }
}

/** Reads the content of a symlink into the given Buffer.

  @param path The path to the symlink.
  @param stats The stats of the symlink.
  @param buffer_ptr Buffer for storing the string. This buffer must have
  been created by CR_RegionAllocGrowable() or must point to NULL otherwise.
  If it points to NULL, a new buffer will be allocated by
  CR_EnsureCapacity(), to which the given pointer will be assigned.
*/
void readSymlink(StringView path, const struct stat stats,
                 char **buffer_ptr)
{
  const uint64_t buffer_length = sUint64Add(stats.st_size, 1);
  if(buffer_length > SIZE_MAX)
  {
    die("symlink does not fit in memory: \"%s\"", path.content);
  }
  else if(buffer_length > SSIZE_MAX)
  {
    /* In this case the behaviour of readlink() is implementation
       dependent and not portable. */
    die("symlink is too large: \"%s\"", path.content);
  }

  *buffer_ptr = CR_EnsureCapacity(*buffer_ptr, buffer_length);

  /* Although st_size bytes are enough to store the symlinks target path,
     the full buffer is used. This allows to detect whether the symlink
     has increased in size since its last lstat() or not. */
  const ssize_t read_bytes =
    readlink(path.content, *buffer_ptr, buffer_length);

  if(read_bytes == -1)
  {
    dieErrno("failed to read symlink: \"%s\"", path.content);
  }
  else if(read_bytes != stats.st_size)
  {
    die("symlink changed while reading: \"%s\"", path.content);
  }

  (*buffer_ptr)[stats.st_size] = '\0';
}

/** Compares the node against the stats in the given results and updates
  both its backup hint and the specified path state.

  @param node The node containing the hint to update.
  @param state The state to update.
  @param stats The stats of the file represented by the given node.
*/
void applyNodeChanges(PathNode *node, PathState *state,
                      const struct stat stats)
{
  if(state->uid != stats.st_uid || state->gid != stats.st_gid)
  {
    backupHintSet(node->hint, BH_owner_changed);
    state->uid = stats.st_uid;
    state->gid = stats.st_gid;
  }

  /* Path state specific change checks. */
  if(state->type == PST_regular_file)
  {
    if(state->metadata.file_info.permission_bits != stats.st_mode)
    {
      backupHintSet(node->hint, BH_permissions_changed);
      state->metadata.file_info.permission_bits = stats.st_mode;
    }

    if(state->metadata.file_info.modification_time != stats.st_mtime)
    {
      backupHintSet(node->hint, BH_timestamp_changed);
      state->metadata.file_info.modification_time = stats.st_mtime;
    }

    if(state->metadata.file_info.size != (uint64_t)stats.st_size)
    {
      backupHintSet(node->hint, BH_content_changed);
      state->metadata.file_info.size = stats.st_size;
    }
    else if((node->hint & BH_timestamp_changed) &&
            state->metadata.file_info.size > 0)
    {
      checkFileContentChanges(node, state, stats);
    }
  }
  else if(state->type == PST_symlink)
  {
    static char *buffer = NULL;
    readSymlink(node->path, stats, &buffer);

    if(!strEqual(state->metadata.symlink_target, str(buffer)))
    {
      strSet(&state->metadata.symlink_target,
             strLegacyCopy(str(buffer)));
      backupHintSet(node->hint, BH_content_changed);
    }
  }
  else if(state->type == PST_directory)
  {
    if(state->metadata.directory_info.permission_bits != stats.st_mode)
    {
      backupHintSet(node->hint, BH_permissions_changed);
      state->metadata.directory_info.permission_bits = stats.st_mode;
    }

    if(state->metadata.directory_info.modification_time != stats.st_mtime)
    {
      backupHintSet(node->hint, BH_timestamp_changed);
      state->metadata.directory_info.modification_time = stats.st_mtime;
    }
  }
}
