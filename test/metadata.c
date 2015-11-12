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
  Tests repository metadata handling.
*/

#include "metadata.h"

#include "test.h"
#include "memory-pool.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** Counts the subnodes of the given node.

  @param parent_node The node containing the subnodes.

  @return The subnode count.
*/
static size_t countSubnodes(PathNode *parent_node)
{
  size_t subnode_count = 0;

  for(PathNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
  {
    subnode_count++;
  }

  return subnode_count;
}

/** Initializes a history point.

  @param metadata The metadata, containing the backup history array.
  @param index The index of the history point in the array, which should be
  initialized.
  @param id The backup id which should be assigned to the point.
  @param timestamp The timestamp to be assigned to the point.
*/
static void initHistPoint(Metadata *metadata, size_t index,
                          size_t id, time_t timestamp)
{
  metadata->backup_history[index].id = id;
  metadata->backup_history[index].timestamp = timestamp;
  metadata->backup_history[index].ref_count = 0;
}

/** Counterpart to initHistPoint() which additionally takes the reference
  count of the point. */
static void checkHistPoint(Metadata *metadata, size_t index, size_t id,
                           time_t timestamp, size_t ref_count)
{
  assert_true(metadata->backup_history[index].id == id);
  assert_true(metadata->backup_history[index].timestamp == timestamp);
  assert_true(metadata->backup_history[index].ref_count == ref_count);
}

/** Creates a new path node.

  @param path_str The node name, which will be appended to the parent nodes
  path.
  @param policy The policy of the created node.
  @param parent_node The parent node, in which the new node should be
  stored. Can be NULL, if the new node shouldn't have a parent node.
  @param metadata The metadata to which the current node belongs to. It
  will be updated by this function.

  @return A new node which should not be freed by the caller.
*/
static PathNode *createPathNode(const char *path_str, BackupPolicy policy,
                                PathNode *parent_node, Metadata *metadata)
{
  PathNode *node = mpAlloc(sizeof *node);

  node->policy = policy;
  node->history = NULL;
  node->subnodes = NULL;

  if(parent_node == NULL)
  {
    String path = strAppendPath(str(""), str(path_str));
    memcpy(&node->path, &path, sizeof(node->path));

    node->next = NULL;
  }
  else
  {
    String path = strAppendPath(parent_node->path, str(path_str));
    memcpy(&node->path, &path, sizeof(node->path));

    node->next = parent_node->subnodes;
    parent_node->subnodes = node;
  }

  strtableMap(metadata->path_table, node->path, node);
  metadata->total_path_count = sSizeAdd(metadata->total_path_count, 1);

  return node;
}

/** Appends a new history point to the given node.

  @param node The node to which the history point should be appended to.
  @param backup The backup, to which the history point belongs to.
  @param metadata The metadata to which the node belongs to.
  @param state The state of the backup.
*/
static void appendHist(PathNode *node, Backup *backup, PathState state)
{
  PathHistory *history_point = mpAlloc(sizeof *history_point);

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

  history_point->state = state;
  history_point->next = NULL;
}

/** Assigns the given values to the specified path state as defined in the
  documentation of RegularMetadata.

  @param state A path state which must have the type PST_regular.
  @param size The size of the file described by the path state.
  @param hash The hash of the file or the files entire content, depending
  on the files size.
  @param slot The slot number of the file in the repository. Will be
  ignored if the files size is not greater than SHA_DIGEST_LENGTH.
*/
static void assignRegularValues(PathState *state, uint64_t size,
                                uint8_t *hash ,uint8_t slot)
{
  state->metadata.reg.size = size;

  if(size > SHA_DIGEST_LENGTH)
  {
    memcpy(state->metadata.reg.hash, hash, SHA_DIGEST_LENGTH);
    state->metadata.reg.slot = slot;
  }
  else if(size > 0)
  {
    memcpy(state->metadata.reg.hash, hash, size);
  }
}

/** Returns true if the specified regular path state contains the given
  values. Counterpart to assignRegularValues(). */
static bool checkRegularValues(PathState *state, uint64_t size,
                               uint8_t *hash, uint8_t slot)
{
  if(state->metadata.reg.size != size)
  {
    return false;
  }
  else if(size > SHA_DIGEST_LENGTH)
  {
    return (memcmp(state->metadata.reg.hash, hash, SHA_DIGEST_LENGTH) == 0)
      && state->metadata.reg.slot == slot;
  }
  else if(size > 0)
  {
    return memcmp(state->metadata.reg.hash, hash, size) == 0;
  }
  else
  {
    return true;
  }
}

/** A wrapper around appendHist(), which appends a path state with the type
  PST_non_existing. */
static void appendHistNonExisting(PathNode *node, Backup *backup)
{
  appendHist(node, backup, (PathState){ .type = PST_non_existing });
}

/** A wrapper around appendHist(), which appends the path state of a
  regular file. It takes the following additional arguments:

  @param uid The user id of the files owner.
  @param gid The group id of the files owner.
  @param timestamp The modification time of the file.
  @param mode The permission bits of the file.
  @param size The files size.
  @param hash A pointer to the hash of the file. Will be ignored if the
  file size is 0. Otherwise it will be defined like in the documentation of
  RegularMetadata.
  @param slot The slot number of the corresponding file in the repository.
  Will be ignored if the file size is not bigger than SHA_DIGEST_LENGTH.
*/
static void appendHistRegular(PathNode *node, Backup *backup, uid_t uid,
                              gid_t gid, time_t timestamp, mode_t mode,
                              uint64_t size, uint8_t *hash, uint8_t slot)
{
  PathState state =
  {
    .type = PST_regular,
    .uid = uid,
    .gid = gid,
    .timestamp = timestamp
  };

  state.metadata.reg.mode = mode;
  assignRegularValues(&state, size, hash, slot);
  appendHist(node, backup, state);
}

/** A wrapper around appendHist(), which appends the path state of a
  symbolic link to the given node. It is like appendHistRegular(), but
  takes the following additional arguments:

  @param sym_target The target path of the symlink. The created history
  point will keep a reference to this string, so make sure not to mutate it
  as long as the history point is in use.
*/
static void appendHistSymlink(PathNode *node, Backup *backup, uid_t uid,
                              gid_t gid, time_t timestamp,
                              const char *sym_target)
{
  PathState state =
  {
    .type = PST_symlink,
    .uid = uid,
    .gid = gid,
    .timestamp = timestamp,
    .metadata.sym_target = sym_target
  };

  appendHist(node, backup, state);
}

/** Like appendHistRegular(), but for a directory. */
static void appendHistDirectory(PathNode *node, Backup *backup, uid_t uid,
                                gid_t gid, time_t timestamp, mode_t mode)
{
  PathState state =
  {
    .type = PST_directory,
    .uid = uid,
    .gid = gid,
    .timestamp = timestamp,
    .metadata.dir_mode = mode
  };

  appendHist(node, backup, state);
}

/** Appends the history point of a config file to the metadatas config
  history.

  @param metadata The metadata struct containing the history.
  @param backup The backup, to which the history point belongs.
  @param size The size of the config file at the backup point.
  @param hash The hash of the config file during the backup point. Read the
  documentation of RegularMetadata for more informations on how and when
  the hash will be stored.
  @param slot The slot number of the corresponding file in the repository.
  Will be ignored if the file size is not greater than SHA_DIGEST_LENGTH.
*/
static void appendConfHist(Metadata *metadata, Backup *backup,
                           uint64_t size, uint8_t *hash, uint8_t slot)
{
  PathHistory *history_point = mpAlloc(sizeof *history_point);

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

  history_point->state =
    (PathState)
    {
      .type = PST_regular,
      .uid = 0,
      .gid = 0,
      .timestamp = 0,
    };

  history_point->state.metadata.reg.mode = 0;
  assignRegularValues(&history_point->state, size, hash, slot);
  history_point->next = NULL;
}

/** Checks a path tree recursively and terminates the program on errors.

  @param parent_node The first node in the list, which should be checked
  recursively.
  @param metadata The metadata to which the tree belongs.

  @return The amount of path nodes in the entire tree.
*/
static size_t checkPathTree(PathNode *parent_node, Metadata *metadata)
{
  size_t count = 0;

  for(PathNode *node = parent_node; node != NULL; node = node->next)
  {
    if(node->path.str[node->path.length] != '\0')
    {
      die("unterminated path string in metadata: \"%s\"",
          strCopy(node->path).str);
    }
    else if(strtableGet(metadata->path_table, node->path) == NULL)
    {
      die("path was not mapped in metadata: \"%s\"", node->path.str);
    }
    else if(node->history == NULL)
    {
      die("path has no history: \"%s\"", node->path.str);
    }

    count += checkPathTree(node->subnodes, metadata);
    count++;
  }

  return count;
}

/** Performs some basic checks on the given metadatas config history.

  @param metadata The metadata struct containing the config file history.

  @return The history length of the config file.
*/
static size_t checkConfHist(Metadata *metadata)
{
  size_t history_length = 0;

  for(PathHistory *point = metadata->config_history;
      point != NULL; point = point->next)
  {
    if(point->state.type != PST_regular)
    {
      die("config history point doesn't represent a regular file");
    }
    else if(point->next != NULL &&
            point->backup->id >= point->next->backup->id)
    {
      die("config history has an invalid order");
    }

    history_length++;
  }

  return history_length;
}

/** Assert that the given metadata contains a config history point with the
  specified properties. Counterpart to appendConfHist(). */
static void mustHaveConf(Metadata *metadata, Backup *backup,
                         uint64_t size, uint8_t *hash, uint8_t slot)
{
  for(PathHistory *point = metadata->config_history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       checkRegularValues(&point->state, size, hash, slot))
    {
      return;
    }
  }

  die("config history point with id %zu doesn't exist", backup->id);
}

/** Performs some basic checks on a path nodes history.

  @param node The node containing the history.

  @return The length of the nodes history.
*/
static size_t checkNodeHist(PathNode *node)
{
  size_t history_length = 0;

  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->next != NULL &&
            point->backup->id >= point->next->backup->id)
    {
      die("path node history has an invalid order: \"%s\"",
          node->path.str);
    }
    else if(point->state.type > PST_directory)
    {
      die("node history point has an invalid state type: \"%s\"",
          node->path.str);
    }

    history_length++;
  }

  return history_length;
}

/** Assert that the given node has a non-existing path state at the given
  backup point. Counterpart to appendHistNonExisting(). */
static void mustHaveNonExisting(PathNode *node, Backup *backup)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_non_existing)
    {
      return;
    }
  }

  die("node \"%s\" has no non-existing history point at backup %zu",
      node->path.str, backup->id);
}

/** Assert that the given node contains a history point with the specified
  properties. Counterpart to appendHistRegular(). */
static void mustHaveRegular(PathNode *node, Backup *backup, uid_t uid,
                            gid_t gid, time_t timestamp, mode_t mode,
                            uint64_t size, uint8_t *hash, uint8_t slot)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_regular &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       point->state.metadata.reg.mode == mode &&
       checkRegularValues(&point->state, size, hash, slot))
    {
      return;
    }
  }

  die("path node \"%s\" has no regular path state in its history",
      node->path.str);
}

/** Assert that the given node contains a symlink history point with the
  specified properties. Counterpart to appendHistSymlink(). */
static void mustHaveSymlink(PathNode *node, Backup *backup, uid_t uid,
                            gid_t gid, time_t timestamp,
                            const char *sym_target)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_symlink &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       strcmp(point->state.metadata.sym_target, sym_target) == 0)
    {
      return;
    }
  }

  die("path node \"%s\" doesn't have the symlink \"%s\" in its history",
      node->path.str, sym_target);
}

/** Assert that the given node contains a directory history point with the
  specified properties. Counterpart to appendHistDirectory(). */
static void mustHaveDirectory(PathNode *node, Backup *backup, uid_t uid,
                              gid_t gid, time_t timestamp, mode_t mode)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == backup &&
       point->state.type == PST_directory &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       point->state.metadata.dir_mode == mode)
    {
      return;
    }
  }

  die("path node \"%s\" was not a directory at backup point %zu",
      node->path.str, backup->id);
}

/** Creates an empty metadata tree and initializes some of its variables.

  @param backup_history_length The amount of elements in the backup history
  which should be allocated.

  @return A new metadata tree which should not be freed by the caller.
*/
static Metadata *createEmptyMetadata(size_t backup_history_length)
{
  Metadata *metadata = mpAlloc(sizeof *metadata);

  metadata->current_backup.id = 0;
  metadata->current_backup.timestamp = 0;
  metadata->current_backup.ref_count = 0;

  metadata->backup_history_length = backup_history_length;
  if(backup_history_length == 0)
  {
    metadata->backup_history = NULL;
  }
  else
  {
    metadata->backup_history =
      mpAlloc(sSizeMul(sizeof *metadata->backup_history,
                       metadata->backup_history_length));
  }

  metadata->config_history = NULL;
  metadata->total_path_count = 0;
  metadata->path_table = strtableNewFixed(8);
  metadata->paths = NULL;

  return metadata;
}

/** Performs some basic checks on a metadata struct.

  @param metadata The metadata struct to be checked.
  @param config_history_length The length of the config history which the
  given metadata must have.
*/
static void checkMetadata(Metadata *metadata, size_t config_history_length)
{
  assert_true(metadata != NULL);
  assert_true(metadata->current_backup.id == 0);
  assert_true(metadata->current_backup.timestamp == 0);

  if(metadata->backup_history_length == 0)
  {
    assert_true(metadata->backup_history == NULL);
  }
  else
  {
    assert_true(metadata->backup_history != NULL);
  }

  assert_true(checkConfHist(metadata) == config_history_length);
  assert_true(metadata->path_table != NULL);
  assert_true(metadata->total_path_count ==
              checkPathTree(metadata->paths, metadata));
}

/** Finds a specific node in the given PathNode list. If the node couldn't
  be found, the program will be terminated with failure.

  @param start_node The beginning of the list.
  @param path_str The name of the node which should be found.
  @param policy The policy of the node.
  @param history_length The history length of the node.
  @param subnode_count The amount of subnodes.

  @return The node with the specified properties.
*/
static PathNode *findNode(PathNode *start_node, const char *path_str,
                          BackupPolicy policy, size_t history_length,
                          size_t subnode_count)
{
  String path = str(path_str);
  for(PathNode *node = start_node; node != NULL; node = node->next)
  {
    if(strCompare(node->path, path) && node->policy == policy &&
       checkNodeHist(node) == history_length &&
       countSubnodes(node) == subnode_count)
    {
      return node;
    }
  }

  die("node \"%s\" with the specified properties does not exist",
      path_str);
  return NULL;
}

/** Generates test metadata, that can be tested with checkTestData1().

  @return A Metadata struct that should not be freed by the caller.
*/
static Metadata *genTestData1(void)
{
  Metadata *metadata = createEmptyMetadata(4);
  initHistPoint(metadata, 0, 0, 1234);
  initHistPoint(metadata, 1, 1, -1334953412);
  initHistPoint(metadata, 2, 2, 7890);
  initHistPoint(metadata, 3, 3, 9876);

  appendConfHist(metadata, &metadata->backup_history[1],
                 131, (uint8_t *)"9a2c1f8130eb0cdef201", 0);
  appendConfHist(metadata, &metadata->backup_history[3],
                 21,  (uint8_t *)"f8130eb0cdef2019a2c1", 98);

  PathNode *etc = createPathNode("etc", BPOL_none, NULL, metadata);
  appendHistDirectory(etc, &metadata->backup_history[3], 12, 8, INT32_MAX, 0777);
  metadata->paths = etc;

  PathNode *conf_d = createPathNode("conf.d", BPOL_none, etc, metadata);
  appendHistDirectory(conf_d, &metadata->backup_history[3], 3, 5, 102934, 0123);

  appendHistRegular(createPathNode("foo", BPOL_mirror, conf_d, metadata),
                    &metadata->backup_history[3], 91, 47, 680123, 0223, 20,
                    (uint8_t *)"66f69cd1998e54ae5533", 122);

  appendHistRegular(createPathNode("bar", BPOL_mirror, conf_d, metadata),
                    &metadata->backup_history[2], 89, 20, 310487, 0523, 48,
                    (uint8_t *)"fffffcd1998e54ae5a70", 12);

  PathNode *portage = createPathNode("portage", BPOL_track, etc, metadata);
  appendHistDirectory(portage, &metadata->backup_history[2], 89, 98, 91234, 0321);
  appendHistDirectory(portage, &metadata->backup_history[3], 7,  19, 12837, 0666);

  PathNode *make_conf =
    createPathNode("make.conf", BPOL_track, portage, metadata);

  appendHistSymlink(make_conf, &metadata->backup_history[0], 59, 23, 1248,
                    "make.conf.backup");
  appendHistNonExisting(make_conf, &metadata->backup_history[2]);
  appendHistRegular(make_conf, &metadata->backup_history[3], 3, 4, 53238,
                    0713, 192, (uint8_t *)"e78863d5e021dd60c1a2", 0);

  return metadata;
}

/** Checks a Metadata struct generated by genTestData1().

  @param metadata The struct which should be checked.
*/
static void checkTestData1(Metadata *metadata)
{
  checkMetadata(metadata, 2);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 4);

  checkHistPoint(metadata, 0, 0, 1234, 1);
  checkHistPoint(metadata, 1, 1, -1334953412, 1);
  checkHistPoint(metadata, 2, 2, 7890, 3);
  checkHistPoint(metadata, 3, 3, 9876, 6);

  mustHaveConf(metadata, &metadata->backup_history[1], 131,
               (uint8_t *)"9a2c1f8130eb0cdef201", 0);
  mustHaveConf(metadata, &metadata->backup_history[3], 21,
               (uint8_t *)"f8130eb0cdef2019a2c1", 98);

  assert_true(metadata->total_path_count == 6);

  PathNode *etc = findNode(metadata->paths, "/etc", BPOL_none, 1, 2);
  mustHaveDirectory(etc, &metadata->backup_history[3], 12, 8, INT32_MAX, 0777);

  PathNode *conf_d = findNode(etc->subnodes, "/etc/conf.d", BPOL_none, 1, 2);
  mustHaveDirectory(conf_d, &metadata->backup_history[3], 3, 5, 102934, 0123);

  PathNode *foo = findNode(conf_d->subnodes, "/etc/conf.d/foo", BPOL_mirror, 1, 0);
  mustHaveRegular(foo, &metadata->backup_history[3], 91, 47, 680123, 0223,
                  20, (uint8_t *)"66f69cd1998e54ae5533", 48);

  PathNode *bar = findNode(conf_d->subnodes, "/etc/conf.d/bar", BPOL_mirror, 1, 0);
  mustHaveRegular(bar, &metadata->backup_history[2], 89, 20, 310487, 0523,
                  48, (uint8_t *)"fffffcd1998e54ae5a70", 12);

  PathNode *portage = findNode(etc->subnodes, "/etc/portage", BPOL_track, 2, 1);
  mustHaveDirectory(portage, &metadata->backup_history[2], 89, 98, 91234, 0321);
  mustHaveDirectory(portage, &metadata->backup_history[3], 7,  19, 12837, 0666);

  PathNode *make_conf =
    findNode(portage->subnodes, "/etc/portage/make.conf", BPOL_track, 3, 0);
  mustHaveSymlink(make_conf, &metadata->backup_history[0],
                  59, 23, 1248, "make.conf.backup");
  mustHaveNonExisting(make_conf, &metadata->backup_history[2]);
  mustHaveRegular(make_conf, &metadata->backup_history[3], 3, 4, 53238,
                  0713, 192, (uint8_t *)"e78863d5e021dd60c1a2", 0);
}

/** Generates test metadata, that can be tested with checkTestData2().

  @return A Metadata struct that should not be freed by the caller.
*/
static Metadata *genTestData2(void)
{
  Metadata *metadata = createEmptyMetadata(3);
  initHistPoint(metadata, 0, 0, 3487);
  initHistPoint(metadata, 1, 1, 2645);
  initHistPoint(metadata, 2, 2, 9742);

  appendConfHist(metadata, &metadata->backup_history[2],
                 210, (uint8_t *)"0cdef2019a2c1f8130eb", 255);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->backup_history[2], 0, 0,  12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistRegular(bashrc, &metadata->backup_history[0], 983, 57, 1920,
                    0655, 1, (uint8_t *)"8130eb0cdef2019a2c1f", 255);
  appendHistNonExisting(bashrc, &metadata->backup_history[1]);
  appendHistRegular(bashrc, &metadata->backup_history[2], 1000, 75, 9348,
                    0755, 252, (uint8_t *)"cdef2019a2c1f8130eb0", 43);

  PathNode *config = createPathNode(".config", BPOL_track, user, metadata);
  appendHistDirectory(config, &metadata->backup_history[0], 783, 192, INT32_MIN, 0575);

  PathNode *usr = createPathNode("usr", BPOL_copy, NULL, metadata);
  appendHistDirectory(usr, &metadata->backup_history[0], 3497, 2389, 183640, 0655);
  appendHistDirectory(usr, &metadata->backup_history[1], 3497, 2389, 816034, 0565);

  metadata->paths->next = usr;

  return metadata;
}

/** Checks a Metadata struct generated by genTestData2().

  @param metadata The struct which should be checked.
*/
static void checkTestData2(Metadata *metadata)
{
  checkMetadata(metadata, 1);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);

  checkHistPoint(metadata, 0, 0, 3487, 4);
  checkHistPoint(metadata, 1, 1, 2645, 2);
  checkHistPoint(metadata, 2, 2, 9742, 3);

  mustHaveConf(metadata, &metadata->backup_history[2], 210,
               (uint8_t *)"0cdef2019a2c1f8130eb", 255);

  assert_true(metadata->total_path_count == 5);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[2], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 2);
  mustHaveDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 3, 0);
  mustHaveRegular(bashrc, &metadata->backup_history[0], 983, 57, 1920,
                  0655, 1, (uint8_t *)"8???????????????????", 19);
  mustHaveNonExisting(bashrc, &metadata->backup_history[1]);
  mustHaveRegular(bashrc, &metadata->backup_history[2], 1000, 75, 9348,
                  0755, 252, (uint8_t *)"cdef2019a2c1f8130eb0", 43);

  PathNode *config = findNode(user->subnodes, "/home/user/.config", BPOL_track, 1, 0);
  mustHaveDirectory(config, &metadata->backup_history[0], 783, 192, INT32_MIN, 0575);

  PathNode *usr = findNode(metadata->paths, "/usr", BPOL_copy, 2, 0);
  mustHaveDirectory(usr, &metadata->backup_history[0], 3497, 2389, 183640, 0655);
  mustHaveDirectory(usr, &metadata->backup_history[1], 3497, 2389, 816034, 0565);
}

/** Generates a dummy metadata tree with unreferenced history points.
  Writing and loading the metadata tree will strip these unwanted points,
  so that its result can be verified with checkLoadedUnusedBackupPoints().

  @return A tree which should not be freed by the caller.
*/
static Metadata *genUnusedBackupPoints(void)
{
  Metadata *metadata = createEmptyMetadata(6);
  initHistPoint(metadata, 0, 0, 84390);
  initHistPoint(metadata, 1, 1, 140908);
  initHistPoint(metadata, 2, 2, 13098);
  initHistPoint(metadata, 3, 3, -6810);
  initHistPoint(metadata, 4, 4, 54111);
  initHistPoint(metadata, 5, 5, 47622);

  appendConfHist(metadata, &metadata->backup_history[1],
                 3, (uint8_t *)"fbc92e19ee0cd2140faa", 0);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->backup_history[1], 0, 0,  12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->backup_history[3], 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920,
                    0655, 0, (uint8_t *)"8130eb0cdef2019a2c1f", 1);
  appendHistNonExisting(bashrc, &metadata->backup_history[4]);

  PathNode *config = createPathNode(".config", BPOL_track, user, metadata);
  appendHistDirectory(config, &metadata->backup_history[4], 783, 192, 3487901, 0575);

  return metadata;
}

/** Tests a tree generated by genUnusedBackupPoints(), which was written
  and reloaded from disk. */
static void checkLoadedUnusedBackupPoints(Metadata *metadata)
{
  checkMetadata(metadata, 1);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);

  checkHistPoint(metadata, 0, 0, 140908, 3);
  checkHistPoint(metadata, 1, 1, -6810,  1);
  checkHistPoint(metadata, 2, 2, 54111,  2);

  mustHaveConf(metadata, &metadata->backup_history[0], 3,
               (uint8_t *)"fbc?????????????????", 73);

  assert_true(metadata->total_path_count == 4);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[0], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 2);
  mustHaveDirectory(user, &metadata->backup_history[1], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 2, 0);
  mustHaveRegular(bashrc, &metadata->backup_history[0], 983, 57, 1920,
                  0655, 0, (uint8_t *)"xxxxxxxxxxxxxxxxxxxx", 27);
  mustHaveNonExisting(bashrc, &metadata->backup_history[2]);

  PathNode *config = findNode(user->subnodes, "/home/user/.config", BPOL_track, 1, 0);
  mustHaveDirectory(config, &metadata->backup_history[2], 783, 192, 3487901, 0575);
}

/** Generates a dummy metadata tree containing history points, pointing
  at the metadatas current backup. After saving + reloading the tree from
  disk, the current backup point should be merged into the backup history
  and can be checked with checkLoadedCurrentBackupData().

  @return A metadata tree which should not be freed by the caller.
*/
static Metadata *genCurrentBackupData(void)
{
  Metadata *metadata = createEmptyMetadata(2);
  metadata->current_backup.timestamp = 57645;
  initHistPoint(metadata, 0, 0, 48390);
  initHistPoint(metadata, 1, 1, 84908);

  appendConfHist(metadata, &metadata->current_backup,
                 6723, (uint8_t *)"fbc92e19ee0cd2140faa", 76);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->backup_history[0], 0, 0,  12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->current_backup, 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistNonExisting(bashrc, &metadata->current_backup);
  appendHistRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920,
                    0655, 7, (uint8_t *)"8130eb0cdef2019a2c1f", 8);

  return metadata;
}

/** Checks the given metadata struct, which was generated by
  genCurrentBackupData() and then reloaded from disk. */
static void checkLoadedCurrentBackupData(Metadata *metadata)
{
  checkMetadata(metadata, 1);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);

  checkHistPoint(metadata, 0, 0, 57645, 3);
  checkHistPoint(metadata, 1, 1, 48390, 1);
  checkHistPoint(metadata, 2, 2, 84908, 1);

  mustHaveConf(metadata, &metadata->backup_history[0], 6723,
               (uint8_t *)"fbc92e19ee0cd2140faa", 76);

  assert_true(metadata->total_path_count == 3);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[1], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 1);
  mustHaveDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 2, 0);
  mustHaveNonExisting(bashrc, &metadata->backup_history[0]);
  mustHaveRegular(bashrc, &metadata->backup_history[2], 983, 57, 1920,
                  0655, 7, (uint8_t *)"8130eb0-------------", 0);
}

/** Generates an empty dummy metadata tree without a config history. It can
  be checked with checkNoConfHist(). */
static Metadata *genNoConfHist(void)
{
  Metadata *metadata = createEmptyMetadata(3);
  initHistPoint(metadata, 0, 0, 48390);
  initHistPoint(metadata, 1, 1, 84908);
  initHistPoint(metadata, 2, 2, 91834);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->backup_history[0], 0, 0,  12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->backup_history[2], 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistNonExisting(bashrc, &metadata->backup_history[0]);
  appendHistRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920,
                    0655, 579, (uint8_t *)"8130eb0cdef2019a2c1f", 128);

  return metadata;
}

/** Checks the metadata generated by genNoConfHist(). */
static void checkNoConfHist(Metadata *metadata)
{
  checkMetadata(metadata, 0);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->config_history == NULL);

  checkHistPoint(metadata, 0, 0, 48390, 2);
  checkHistPoint(metadata, 1, 1, 84908, 1);
  checkHistPoint(metadata, 2, 2, 91834, 1);

  assert_true(metadata->total_path_count == 3);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[0], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 1);
  mustHaveDirectory(user, &metadata->backup_history[2], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 2, 0);
  mustHaveNonExisting(bashrc, &metadata->backup_history[0]);
  mustHaveRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920,
                  0655, 579, (uint8_t *)"8130eb0cdef2019a2c1f", 128);
}

/** Generates a dummy metadata struct with no path tree. It can be checked
  with checkNoPathTree(). */
static Metadata *genNoPathTree(void)
{
  Metadata *metadata = createEmptyMetadata(2);
  initHistPoint(metadata, 0, 0, 3249);
  initHistPoint(metadata, 1, 1, 29849483);

  appendConfHist(metadata, &metadata->backup_history[0],
                 19, (uint8_t *)"fbc92e19ee0cd2140faa", 34);
  appendConfHist(metadata, &metadata->backup_history[1],
                 103894, (uint8_t *)"some test bytes?????", 35);

  return metadata;
}

/** Checks the metadata generated by genNoPathTree(). */
static void checkNoPathTree(Metadata *metadata)
{
  checkMetadata(metadata, 2);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 2);

  checkHistPoint(metadata, 0, 0, 3249, 1);
  checkHistPoint(metadata, 1, 1, 29849483, 1);

  mustHaveConf(metadata, &metadata->backup_history[0],
               19, (uint8_t *)"fbc92e19ee0cd2140fa%", 8);
  mustHaveConf(metadata, &metadata->backup_history[1],
               103894, (uint8_t *)"some test bytes?????", 35);

  assert_true(metadata->total_path_count == 0);
  assert_true(metadata->paths == NULL);
}

/** Generates an empty dummy metadata tree, which contains only
  unreferenced backup points. After writing and loading this metadata from
  disc, it can be checked with checkEmptyMetadata().

  @return A new metadata struct which should not be freed by the caller.
*/
static Metadata *genWithOnlyBackupPoints(void)
{
  Metadata *metadata = createEmptyMetadata(3);
  initHistPoint(metadata, 0, 0, 3249);
  initHistPoint(metadata, 1, 1, 29849483);
  initHistPoint(metadata, 2, 2, 1347);

  return metadata;
}

/** Checks an empty metadata tree. */
static void checkEmptyMetadata(Metadata *metadata)
{
  checkMetadata(metadata, 0);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->config_history == NULL);
  assert_true(metadata->total_path_count == 0);
  assert_true(metadata->paths == NULL);
}

/** Initializes the given metadata tree, so that it only contains history
  points pointing to the current backup. After reloading the metadata from
  disk, it can be verified with checkOnlyCurrentBackupData().

  @param metadata A valid metadata struct which should be initialized.

  @return The same metadata that was passed to this function.
*/
static Metadata *initOnlyCurrentBackupData(Metadata *metadata)
{
  metadata->current_backup.timestamp = 1348981;

  appendConfHist(metadata, &metadata->current_backup,
                 6723, (uint8_t *)"fbc92e19ee0cd2140faa", 1);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->current_backup, 0, 0,  12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->current_backup, 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistRegular(bashrc, &metadata->current_backup, 983, 57, -1,
                    0655, 0, (uint8_t *)"8130eb0cdef2019a2c1f", 127);

  return metadata;
}

/** Counterpart to initOnlyCurrentBackupData(). */
static void checkOnlyCurrentBackupData(Metadata *metadata)
{
  checkMetadata(metadata, 1);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 1);

  checkHistPoint(metadata, 0, 0, 1348981, 4);

  mustHaveConf(metadata, &metadata->backup_history[0], 6723,
               (uint8_t *)"fbc92e19ee0cd2140faa", 1);

  assert_true(metadata->total_path_count == 3);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[0], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 1);
  mustHaveDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 1, 0);
  mustHaveRegular(bashrc, &metadata->backup_history[0], 983, 57, -1,
                  0655, 0, (uint8_t *)"....................", 217);
}

int main(void)
{
  testGroupStart("newMetadata()");
  checkEmptyMetadata(newMetadata());
  testGroupEnd();

  testGroupStart("reading and writing of metadata");
  /* Write and read TestData1. */
  Metadata *test_data_1 = genTestData1();
  checkTestData1(test_data_1);

  writeMetadata(test_data_1, "tmp");
  checkTestData1(loadMetadata("tmp/metadata"));

  /* Write and read TestData2. */
  Metadata *test_data_2 = genTestData2();
  checkTestData2(test_data_2);

  writeMetadata(test_data_2, "tmp");
  checkTestData2(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("writing only referenced backup points");
  Metadata *unused_backup_points = genUnusedBackupPoints();
  writeMetadata(unused_backup_points, "tmp");
  checkLoadedUnusedBackupPoints(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("merging current backup point while writing");
  Metadata *current_backup_data = genCurrentBackupData();
  writeMetadata(current_backup_data, "tmp");
  checkLoadedCurrentBackupData(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("adjust backup ID order");
  test_data_1->backup_history[0].id = 3;
  test_data_1->backup_history[1].id = 2;
  test_data_1->backup_history[2].id = 1;
  test_data_1->backup_history[3].id = 0;
  writeMetadata(test_data_1, "tmp");
  checkTestData1(loadMetadata("tmp/metadata"));

  test_data_1->backup_history[0].id = 12;
  test_data_1->backup_history[1].id = 8;
  test_data_1->backup_history[2].id = 12983948;
  test_data_1->backup_history[3].id = 0;
  writeMetadata(test_data_1, "tmp");
  checkTestData1(loadMetadata("tmp/metadata"));

  test_data_2->backup_history[0].id = 0;
  test_data_2->backup_history[1].id = 0;
  test_data_2->backup_history[2].id = 0;
  writeMetadata(test_data_2, "tmp");
  checkTestData2(loadMetadata("tmp/metadata"));

  unused_backup_points->backup_history[0].id = 0;
  unused_backup_points->backup_history[1].id = 35;
  unused_backup_points->backup_history[2].id = 982;
  unused_backup_points->backup_history[3].id = 982;
  unused_backup_points->backup_history[4].id = 5;
  unused_backup_points->backup_history[5].id = 0;
  writeMetadata(unused_backup_points, "tmp");
  checkLoadedUnusedBackupPoints(loadMetadata("tmp/metadata"));

  current_backup_data->backup_history[0].id = 70;
  current_backup_data->backup_history[1].id = 70;
  writeMetadata(current_backup_data, "tmp");
  checkLoadedCurrentBackupData(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("no config history");
  Metadata *no_conf_hist = genNoConfHist();
  checkNoConfHist(no_conf_hist);
  writeMetadata(no_conf_hist, "tmp");
  checkNoConfHist(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("no path tree");
  Metadata *no_path_tree = genNoPathTree();
  checkNoPathTree(no_path_tree);
  writeMetadata(no_path_tree, "tmp");
  checkNoPathTree(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("no config history and no path tree");
  Metadata *no_conf_no_paths = genWithOnlyBackupPoints();
  writeMetadata(no_conf_no_paths, "tmp");
  checkEmptyMetadata(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("empty metadata");
  Metadata *empty_metadata = createEmptyMetadata(0);
  checkEmptyMetadata(empty_metadata);
  writeMetadata(empty_metadata, "tmp");
  checkEmptyMetadata(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("merging current backup into empty metadata");
  writeMetadata(initOnlyCurrentBackupData(createEmptyMetadata(0)), "tmp");
  checkOnlyCurrentBackupData(loadMetadata("tmp/metadata"));

  /* The same test as above, but with unreferenced backup points, which
     should be discarded while writing. */
  writeMetadata(initOnlyCurrentBackupData(genWithOnlyBackupPoints()), "tmp");
  checkOnlyCurrentBackupData(loadMetadata("tmp/metadata"));
  testGroupEnd();

  testGroupStart("reject corrupted metadata");
  assert_error(loadMetadata("non-existing.txt"),
               "failed to access \"non-existing.txt\": No such file or directory");
  assert_error(loadMetadata("generated-broken-metadata/missing-byte"),
               "corrupted metadata: expected 1 byte, got 0: \"generated-broken-metadata/missing-byte\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-slot"),
               "corrupted metadata: expected 1 byte, got 0: \"generated-broken-metadata/missing-slot\"");
  assert_error(loadMetadata("generated-broken-metadata/invalid-path-state-type"),
               "invalid PathStateType in \"generated-broken-metadata/invalid-path-state-type\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-path-state-type"),
               "corrupted metadata: expected 1 byte, got 0: \"generated-broken-metadata/missing-path-state-type\"");
  assert_error(loadMetadata("generated-broken-metadata/incomplete-32-bit-value"),
               "corrupted metadata: expected 4 bytes, got 3: \"generated-broken-metadata/incomplete-32-bit-value\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-32-bit-value"),
               "corrupted metadata: expected 4 bytes, got 0: \"generated-broken-metadata/missing-32-bit-value\"");
  assert_error(loadMetadata("generated-broken-metadata/incomplete-size"),
               "corrupted metadata: expected 8 bytes, got 3: \"generated-broken-metadata/incomplete-size\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-size"),
               "corrupted metadata: expected 8 bytes, got 0: \"generated-broken-metadata/missing-size\"");
  assert_error(loadMetadata("generated-broken-metadata/incomplete-time"),
               "corrupted metadata: expected 8 bytes, got 7: \"generated-broken-metadata/incomplete-time\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-time"),
               "corrupted metadata: expected 8 bytes, got 0: \"generated-broken-metadata/missing-time\"");
  assert_error(loadMetadata("generated-broken-metadata/incomplete-hash"),
               "corrupted metadata: expected 20 bytes, got 5: \"generated-broken-metadata/incomplete-hash\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-hash"),
               "corrupted metadata: expected 20 bytes, got 0: \"generated-broken-metadata/missing-hash\"");
  assert_error(loadMetadata("generated-broken-metadata/incomplete-path"),
               "corrupted metadata: expected 7 bytes, got 4: \"generated-broken-metadata/incomplete-path\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-path"),
               "corrupted metadata: expected 3 bytes, got 0: \"generated-broken-metadata/missing-path\"");
  assert_error(loadMetadata("generated-broken-metadata/incomplete-symlink-target-path"),
               "corrupted metadata: expected 16 bytes, got 6: \"generated-broken-metadata/incomplete-symlink-target-path\"");
  assert_error(loadMetadata("generated-broken-metadata/missing-symlink-target-path"),
               "corrupted metadata: expected 16 bytes, got 0: \"generated-broken-metadata/missing-symlink-target-path\"");
  assert_error(loadMetadata("generated-broken-metadata/last-byte-missing"),
               "corrupted metadata: expected 8 bytes, got 7: \"generated-broken-metadata/last-byte-missing\"");
  assert_error(loadMetadata("generated-broken-metadata/unneeded-trailing-bytes"),
               "unneeded trailing bytes in \"generated-broken-metadata/unneeded-trailing-bytes\"");
  assert_error(loadMetadata("generated-broken-metadata/path-count-zero"),
               "unneeded trailing bytes in \"generated-broken-metadata/path-count-zero\"");
  testGroupEnd();
}
