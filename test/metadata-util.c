#include "metadata-util.h"

#include "safe-math.h"
#include "test-common.h"
#include "test.h"

/** Creates an empty metadata tree and initializes some of its variables.

  @param backup_history_length The amount of elements in the backup history
  which should be allocated.
*/
Metadata *createEmptyMetadata(CR_Region *r, const size_t backup_history_length)
{
  Metadata *metadata = CR_RegionAlloc(r, sizeof *metadata);
  metadata->r = r;

  metadata->current_backup.id = 0;
  metadata->current_backup.completion_time = 0;
  metadata->current_backup.ref_count = 0;

  metadata->backup_history_length = backup_history_length;
  if(backup_history_length == 0)
  {
    metadata->backup_history = NULL;
  }
  else
  {
    metadata->backup_history =
      CR_RegionAlloc(metadata->r, sSizeMul(sizeof *metadata->backup_history, metadata->backup_history_length));
  }

  metadata->config_history = NULL;
  metadata->total_path_count = 0;
  metadata->path_table = strTableNew(r);
  metadata->paths = NULL;

  return metadata;
}

void initHistPoint(Metadata *metadata, const size_t index, const size_t id, const time_t modification_time)
{
  metadata->backup_history[index].id = id;
  metadata->backup_history[index].completion_time = modification_time;
  metadata->backup_history[index].ref_count = 0;
}

/** Creates a new path node.

  @param path_str The node name, which will be appended to the parent nodes
  path.
  @param parent_node The parent node, in which the new node should be
  stored. Can be NULL, if the new node shouldn't have a parent node.
  @param metadata The metadata to which the current node belongs to. It
  will be updated by this function.

  @return A new node which should not be freed by the caller.
*/
PathNode *createPathNode(const char *path_str, const BackupPolicy policy, PathNode *parent_node,
                         Metadata *metadata)
{
  PathNode *node = CR_RegionAlloc(metadata->r, sizeof *node);
  Allocator *a = allocatorWrapRegion(metadata->r);

  node->hint = BH_none;
  node->policy = policy;
  node->history = NULL;
  node->subnodes = NULL;

  if(parent_node == NULL)
  {
    StringView path = strAppendPath(str(""), str(path_str), a);
    strSet(&node->path, path);

    node->next = NULL;
  }
  else
  {
    StringView path = strAppendPath(parent_node->path, str(path_str), a);
    strSet(&node->path, path);

    node->next = parent_node->subnodes;
    parent_node->subnodes = node;
  }

  strTableMap(metadata->path_table, node->path, node);
  metadata->total_path_count = sSizeAdd(metadata->total_path_count, 1);

  return node;
}

/**
  @param state A path state which must have the type PST_regular.
  @param hash The hash of the file or the files entire content, depending
  on the files size.
  @param slot The slot number of the file in the repository. Will be
  ignored if the files size is not greater than FILE_HASH_SIZE.
*/
void assignRegularValues(PathState *state, const mode_t permission_bits, const time_t modification_time,
                         const uint64_t size, const uint8_t *hash, const uint8_t slot)
{
  state->metadata.file_info.permission_bits = permission_bits;
  state->metadata.file_info.modification_time = modification_time;
  state->metadata.file_info.size = size;

  if(size > FILE_HASH_SIZE)
  {
    memcpy(state->metadata.file_info.hash, hash, FILE_HASH_SIZE);
    state->metadata.file_info.slot = slot;
  }
  else if(size > 0)
  {
    memcpy(state->metadata.file_info.hash, hash, size);
  }
}

void appendHist(CR_Region *r, PathNode *node, Backup *backup, const PathState state)
{
  PathHistory *history_point = CR_RegionAlloc(r, sizeof *history_point);

  if(node->history == NULL)
  {
    node->history = history_point;
  }
  else
  {
    PathHistory *last_node = node->history;
    while(last_node->next != NULL)
    {
      last_node = last_node->next;
    }

    last_node->next = history_point;
  }

  history_point->backup = backup;
  backup->ref_count = sSizeAdd(backup->ref_count, 1);

  memcpy(&history_point->state, &state, sizeof(history_point->state));
  history_point->next = NULL;
}

/** A wrapper around appendHist(), which appends a path state with the type
  PST_non_existing. */
void appendHistNonExisting(CR_Region *r, PathNode *node, Backup *backup)
{
  appendHist(r, node, backup, (PathState){ .type = PST_non_existing });
}

/** A wrapper around appendHist(), which appends the path state of a
  regular file.

  @param hash A pointer to the hash of the file. Will be ignored if the
  file size is 0. Otherwise it will be defined like in the documentation of
  RegularMetadata.
  @param slot The slot number of the corresponding file in the repository.
  Will be ignored if the file size is not bigger than FILE_HASH_SIZE.
*/
void appendHistRegular(CR_Region *r, PathNode *node, Backup *backup, const uid_t uid, const gid_t gid,
                       const time_t modification_time, const mode_t permission_bits, const uint64_t size,
                       const uint8_t *hash, const uint8_t slot)
{
  PathState state = {
    .type = PST_regular_file,
    .uid = uid,
    .gid = gid,
  };

  assignRegularValues(&state, permission_bits, modification_time, size, hash, slot);
  appendHist(r, node, backup, state);
}

/** A wrapper around appendHist(), which appends the path state of a
  symbolic link to the given node. It is like appendHistRegular(), but
  takes the following additional arguments:

  @param symlink_target The target path of the symlink. The created history
  point will keep a reference to this string, so make sure not to mutate it
  as long as the history point is in use.
*/
void appendHistSymlink(CR_Region *r, PathNode *node, Backup *backup, const uid_t uid, const gid_t gid,
                       const char *symlink_target)
{
  const PathState state = {
    .type = PST_symlink,
    .uid = uid,
    .gid = gid,
    .metadata.symlink_target = str(symlink_target),
  };

  appendHist(r, node, backup, state);
}

void appendHistDirectory(CR_Region *r, PathNode *node, Backup *backup, const uid_t uid, const gid_t gid,
                         const time_t modification_time, const mode_t permission_bits)
{
  PathState state = {
    .type = PST_directory,
    .uid = uid,
    .gid = gid,
  };

  state.metadata.directory_info.permission_bits = permission_bits;
  state.metadata.directory_info.modification_time = modification_time;
  appendHist(r, node, backup, state);
}

/** Appends the history point of a config file to the metadatas config
  history.

  @param backup The backup, to which the history point belongs.
  @param hash The hash of the config file during the backup point. Read the
  documentation of RegularMetadata for more informations on how and when
  the hash will be stored.
  @param slot The slot number of the corresponding file in the repository.
  Will be ignored if the file size is not greater than FILE_HASH_SIZE.
*/
void appendConfHist(Metadata *metadata, Backup *backup, const uint64_t size, const uint8_t *hash,
                    const uint8_t slot)
{
  PathHistory *history_point = CR_RegionAlloc(metadata->r, sizeof *history_point);

  if(metadata->config_history == NULL)
  {
    metadata->config_history = history_point;
  }
  else
  {
    PathHistory *last_node = metadata->config_history;
    while(last_node->next != NULL)
    {
      last_node = last_node->next;
    }

    last_node->next = history_point;
  }

  history_point->backup = backup;
  backup->ref_count = sSizeAdd(backup->ref_count, 1);

  memset(&history_point->state, 0, sizeof(history_point->state));
  history_point->state.type = PST_regular_file;
  history_point->state.uid = 0;
  history_point->state.gid = 0;

  assignRegularValues(&history_point->state, 0, 0, size, hash, slot);
  history_point->next = NULL;
}
