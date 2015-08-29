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
  @param backup_id The id of the backup, to which the history point belongs
  to. If it is 0, the metadatas current backup will be used. Otherwise it
  will use backups from the metadatas backup history.
  @param metadata The metadata to which the node belongs to.
  @param state The state of the backup.
*/
static void appendHist(PathNode *node, size_t backup_id,
                       Metadata *metadata, PathState state)
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

  Backup *backup = backup_id == 0 ?
    &metadata->current_backup:
    &metadata->backup_history[backup_id - 1];

  history_point->backup = backup;
  backup->ref_count = sSizeAdd(backup->ref_count, 1);

  history_point->state = state;
  history_point->next = NULL;
}

/** A wrapper around appendHist(), which appends a path state with the type
  PST_non_existing. */
static void appendHistNonExisting(PathNode *node, size_t backup_id,
                                  Metadata *metadata)
{
  PathState state = { .type = PST_non_existing };
  appendHist(node, backup_id, metadata, state);
}

/** A wrapper around appendHist(), which appends the path state of a
  regular file. It takes the following additional arguments:

  @param uid The user id of the files owner.
  @param gid The group id of the files owner.
  @param timestamp The modification time of the file.
  @param mode The permission bits of the file.
  @param size The files size.
  @param hash A pointer to the hash of the file.
*/
static void appendHistRegular(PathNode *node, size_t backup_id,
                              Metadata *metadata, uid_t uid, gid_t gid,
                              time_t timestamp, mode_t mode, size_t size,
                              uint8_t *hash)
{
  PathState state =
  {
    .type = PST_regular,
    .uid = uid,
    .gid = gid,
    .timestamp = timestamp,
    .metadata.reg =
    {
      .mode = mode,
      .size = size
    }
  };

  memcpy(&state.metadata.reg.hash, hash, SHA_DIGEST_LENGTH);
  appendHist(node, backup_id, metadata, state);
}

/** A wrapper around appendHist(), which appends the path state of a
  symbolic link to the given node. It is like appendHistRegular(), but
  takes the following additional arguments:

  @param sym_target The target path of the symlink. The created history
  point will keep a reference to this string, so make sure not to mutate it
  as long as the history point is in use.
*/
static void appendHistSymlink(PathNode *node, size_t backup_id,
                              Metadata *metadata, uid_t uid, gid_t gid,
                              time_t timestamp, const char *sym_target)
{
  PathState state =
  {
    .type = PST_symlink,
    .uid = uid,
    .gid = gid,
    .timestamp = timestamp,
    .metadata.sym_target = sym_target
  };

  appendHist(node, backup_id, metadata, state);
}

/** Like appendHistRegular(), but for a directory. */
static void appendHistDirectory(PathNode *node, size_t backup_id,
                                Metadata *metadata, uid_t uid, gid_t gid,
                                time_t timestamp, mode_t mode)
{
  PathState state =
  {
    .type = PST_directory,
    .uid = uid,
    .gid = gid,
    .timestamp = timestamp,
    .metadata.dir_mode = mode
  };

  appendHist(node, backup_id, metadata, state);
}

/** Appends the history point of a config file to the metadatas config
  history.

  @param metadata The metadata struct containing the history.
  @param backup_id The id of the Backup, to which the history point belongs
  to.
  @param file_size The size of the config file at the backup point.
  @param hash The hash of the config file during the backup point.
*/
static void appendConfHist(Metadata *metadata, size_t backup_id,
                           size_t file_size, uint8_t *hash)
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

  Backup *backup = backup_id == 0 ?
    &metadata->current_backup:
    &metadata->backup_history[backup_id - 1];

  history_point->backup = backup;
  backup->ref_count = sSizeAdd(backup->ref_count, 1);

  history_point->state =
    (PathState)
    {
      .type = PST_regular,
      .uid = 0,
      .gid = 0,
      .timestamp = 0,
      .metadata.reg =
      {
        .mode = 0,
        .size = file_size
      }
    };

  memcpy(&history_point->state.metadata.reg.hash, hash, SHA_DIGEST_LENGTH);
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
    if(point->backup == NULL)
    {
      die("config history point doesn't belong to a backup");
    }
    else if((point->backup->id == 0 &&
             point->backup != &metadata->current_backup) ||
            (point->backup->id > 0 &&
             point->backup != &metadata->backup_history[point->backup->id - 1]))
    {
      die("config history points to wrong backup");
    }
    else if(point->state.type != PST_regular)
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
static void mustHaveConf(Metadata *metadata, size_t backup_id,
                         size_t file_size, uint8_t *hash)
{
  for(PathHistory *point = metadata->config_history;
      point != NULL; point = point->next)
  {
    if(point->backup->id == backup_id &&
       point->state.metadata.reg.size == file_size &&
       memcmp(point->state.metadata.reg.hash, hash, SHA_DIGEST_LENGTH) == 0)
    {
      return;
    }
  }

  die("config history point doesn't exist in \"%s\"",
      strCopy(metadata->repo_path).str);
}

/** Performs some basic checks on a path nodes history.

  @param node The node containing the history.

  @return The length of the nodes history.
*/
static size_t checkNodeHist(PathNode *node, Metadata *metadata)
{
  size_t history_length = 0;

  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup == NULL)
    {
      die("node history point doesn't belong to a backup: \"%s\"",
          node->path.str);
    }
    else if((point->backup->id == 0 &&
             point->backup != &metadata->current_backup) ||
            (point->backup->id > 0 &&
             point->backup != &metadata->backup_history[point->backup->id - 1]))
    {
      die("nodes history points to wrong backup: \"%s\"", node->path.str);
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
static void mustHaveNonExisting(PathNode *node, size_t backup_id)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup->id == backup_id &&
       point->state.type == PST_non_existing)
    {
      return;
    }
  }

  die("node \"%s\" has no non-existing history point at backup %zu",
      node->path.str, backup_id);
}

/** Assert that the given node contains a history point with the specified
  properties. Counterpart to appendHistRegular(). */
static void mustHaveRegular(PathNode *node, size_t backup_id,
                            uid_t uid, gid_t gid, time_t timestamp,
                            mode_t mode, size_t size, uint8_t *hash)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup->id == backup_id &&
       point->state.type == PST_regular &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       point->state.metadata.reg.mode == mode &&
       point->state.metadata.reg.size == size &&
       memcmp(point->state.metadata.reg.hash, hash, SHA_DIGEST_LENGTH) == 0)
    {
      return;
    }
  }

  die("path node \"%s\" has no regular path state in its history",
      node->path.str);
}

/** Assert that the given node contains a symlink history point with the
  specified properties. Counterpart to appendHistSymlink(). */
static void mustHaveSymlink(PathNode *node, size_t backup_id,
                            uid_t uid, gid_t gid, time_t timestamp,
                            const char *sym_target)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup->id == backup_id &&
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
static void mustHaveDirectory(PathNode *node, size_t backup_id,
                              uid_t uid, gid_t gid, time_t timestamp,
                              mode_t mode)
{
  for(PathHistory *point = node->history;
      point != NULL; point = point->next)
  {
    if(point->backup->id == backup_id &&
       point->state.type == PST_directory &&
       point->state.uid == uid && point->state.gid == gid &&
       point->state.timestamp == timestamp &&
       point->state.metadata.dir_mode == mode)
    {
      return;
    }
  }

  die("path node \"%s\" was not a directory at backup point %zu",
      node->path.str, backup_id);
}

/** Performs some basic checks on a metadata struct.

  @param metadata The metadata struct to be checked.
  @param repo_path The repository path, which the metadata must contain.
*/
static void checkMetadata(Metadata *metadata, const char *repo_path,
                          size_t config_history_length)
{
  assert_true(metadata != NULL);
  assert_true(metadata->current_backup.id == 0);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(strCompare(metadata->repo_path, str(repo_path)));

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
static PathNode *findNode(Metadata *metadata, PathNode *start_node,
                          const char *path_str, BackupPolicy policy,
                          size_t history_length, size_t subnode_count)
{
  String path = str(path_str);
  for(PathNode *node = start_node; node != NULL; node = node->next)
  {
    if(strCompare(node->path, path) && node->policy == policy &&
       checkNodeHist(node, metadata) == history_length &&
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
  Metadata *metadata = mpAlloc(sizeof *metadata);
  String repo_path = str("foo");
  memcpy(&metadata->repo_path, &repo_path, sizeof(metadata->repo_path));

  metadata->current_backup.id = 0;
  metadata->current_backup.timestamp = 0;
  metadata->current_backup.ref_count = 0;

  metadata->backup_history_length = 4;
  metadata->backup_history =
    mpAlloc(sSizeMul(sizeof *metadata->backup_history,
                     metadata->backup_history_length));

  metadata->backup_history[0].id = 1;
  metadata->backup_history[0].timestamp = 1234;
  metadata->backup_history[0].ref_count = 0;

  metadata->backup_history[1].id = 2;
  metadata->backup_history[1].timestamp = 4321;
  metadata->backup_history[1].ref_count = 0;

  metadata->backup_history[2].id = 3;
  metadata->backup_history[2].timestamp = 7890;
  metadata->backup_history[2].ref_count = 0;

  metadata->backup_history[3].id = 4;
  metadata->backup_history[3].timestamp = 9876;
  metadata->backup_history[3].ref_count = 0;

  appendConfHist(metadata, 3, 131, (uint8_t *)"9a2c1f8130eb0cdef201");
  appendConfHist(metadata, 4, 96,  (uint8_t *)"f8130eb0cdef2019a2c1");

  metadata->total_path_count = 0;
  metadata->path_table = strtableNew();

  PathNode *etc = createPathNode("etc", BPOL_none, NULL, metadata);
  appendHistDirectory(etc, 4, metadata, 12, 8,  2389478, 0777);
  metadata->paths = etc;

  PathNode *conf_d = createPathNode("conf.d", BPOL_none, etc, metadata);
  appendHistDirectory(conf_d, 4, metadata, 3, 5, 102934, 0123);

  appendHistRegular(createPathNode("foo", BPOL_mirror, conf_d, metadata),
                    4, metadata, 91, 47, 680123, 0223, 90,
                    (uint8_t *)"66f69cd1998e54ae5533");

  appendHistRegular(createPathNode("bar", BPOL_mirror, conf_d, metadata),
                    3, metadata, 89, 20, 310487, 0523, 48,
                    (uint8_t *)"fffffcd1998e54ae5a70");

  PathNode *portage = createPathNode("portage", BPOL_track, etc, metadata);
  appendHistDirectory(portage, 3, metadata, 89, 98, 91234, 0321);
  appendHistDirectory(portage, 4, metadata, 7,  19, 12837, 0666);

  PathNode *make_conf =
    createPathNode("make.conf", BPOL_track, portage, metadata);

  appendHistSymlink(make_conf, 1, metadata, 59, 23, 1248,
                    "make.conf.backup");
  appendHistNonExisting(make_conf, 3, metadata);
  appendHistRegular(make_conf, 4, metadata, 3, 4, 53238, 0713, 192,
                    (uint8_t *)"e78863d5e021dd60c1a2");

  return metadata;
}

/** Checks a Metadata struct generated by genTestData1().

  @param metadata The struct which should be checked.
*/
static void checkTestData1(Metadata *metadata)
{
  checkMetadata(metadata, "foo", 2);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 4);

  assert_true(metadata->backup_history[0].id == 1);
  assert_true(metadata->backup_history[0].timestamp == 1234);
  assert_true(metadata->backup_history[0].ref_count == 1);

  assert_true(metadata->backup_history[1].id == 2);
  assert_true(metadata->backup_history[1].timestamp == 4321);
  assert_true(metadata->backup_history[1].ref_count == 0);

  assert_true(metadata->backup_history[2].id == 3);
  assert_true(metadata->backup_history[2].timestamp == 7890);
  assert_true(metadata->backup_history[2].ref_count == 4);

  assert_true(metadata->backup_history[3].id == 4);
  assert_true(metadata->backup_history[3].timestamp == 9876);
  assert_true(metadata->backup_history[3].ref_count == 6);

  mustHaveConf(metadata, 3, 131, (uint8_t *)"9a2c1f8130eb0cdef201");
  mustHaveConf(metadata, 4, 96,  (uint8_t *)"f8130eb0cdef2019a2c1");

  assert_true(metadata->total_path_count == 6);

  PathNode *etc =
    findNode(metadata, metadata->paths, "/etc", BPOL_none, 1, 2);
  mustHaveDirectory(etc, 4, 12, 8, 2389478, 0777);

  PathNode *conf_d = findNode(metadata, etc->subnodes, "/etc/conf.d",
                              BPOL_none, 1, 2);
  mustHaveDirectory(conf_d, 4, 3, 5, 102934, 0123);

  PathNode *foo = findNode(metadata, conf_d->subnodes, "/etc/conf.d/foo",
                           BPOL_mirror, 1, 0);
  mustHaveRegular(foo, 4, 91, 47, 680123, 0223, 90,
                  (uint8_t *)"66f69cd1998e54ae5533");

  PathNode *bar = findNode(metadata, conf_d->subnodes, "/etc/conf.d/bar",
                           BPOL_mirror, 1, 0);
  mustHaveRegular(bar, 3, 89, 20, 310487, 0523, 48,
                  (uint8_t *)"fffffcd1998e54ae5a70");

  PathNode *portage = findNode(metadata, etc->subnodes, "/etc/portage",
                               BPOL_track, 2, 1);
  mustHaveDirectory(portage, 3, 89, 98, 91234, 0321);
  mustHaveDirectory(portage, 4, 7,  19, 12837, 0666);

  PathNode *make_conf =
    findNode(metadata, portage->subnodes, "/etc/portage/make.conf",
             BPOL_track, 3, 0);
  mustHaveSymlink(make_conf, 1,  59, 23, 1248, "make.conf.backup");
  mustHaveNonExisting(make_conf, 3);
  mustHaveRegular(make_conf, 4, 3, 4, 53238, 0713, 192,
                  (uint8_t *)"e78863d5e021dd60c1a2");
}

/** Generates test metadata, that can be tested with checkTestData2().

  @return A Metadata struct that should not be freed by the caller.
*/
static Metadata *genTestData2(void)
{
  Metadata *metadata = mpAlloc(sizeof *metadata);
  String repo_path = str("bar");
  memcpy(&metadata->repo_path, &repo_path, sizeof(metadata->repo_path));

  metadata->current_backup.id = 0;
  metadata->current_backup.timestamp = 0;
  metadata->current_backup.ref_count = 0;

  metadata->backup_history_length = 3;
  metadata->backup_history =
    mpAlloc(sSizeMul(sizeof *metadata->backup_history,
                     metadata->backup_history_length));

  metadata->backup_history[0].id = 1;
  metadata->backup_history[0].timestamp = 3487;
  metadata->backup_history[0].ref_count = 0;

  metadata->backup_history[1].id = 2;
  metadata->backup_history[1].timestamp = 2645;
  metadata->backup_history[1].ref_count = 0;

  metadata->backup_history[2].id = 3;
  metadata->backup_history[2].timestamp = 9742;
  metadata->backup_history[2].ref_count = 0;

  appendConfHist(metadata, 3, 210, (uint8_t *)"0cdef2019a2c1f8130eb");

  metadata->total_path_count = 0;
  metadata->path_table = strtableNew();

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, 3, metadata, 0, 0,  12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, 0, metadata, 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistRegular(bashrc, 3, metadata, 1000, 75, 9348, 0755, 252,
                    (uint8_t *)"cdef2019a2c1f8130eb0");
  appendHistNonExisting(bashrc, 1, metadata);
  appendHistRegular(bashrc, 0, metadata, 983, 57, 1920, 0655, 579,
                    (uint8_t *)"8130eb0cdef2019a2c1f");

  PathNode *config = createPathNode(".config", BPOL_track, user, metadata);
  appendHistDirectory(config, 1, metadata, 783, 192, 3487901, 0575);

  return metadata;
}

/** Checks a Metadata struct generated by genTestData2().

  @param metadata The struct which should be checked.
*/
static void checkTestData2(Metadata *metadata)
{
  checkMetadata(metadata, "bar", 1);
  assert_true(metadata->current_backup.ref_count == 2);
  assert_true(metadata->backup_history_length == 3);

  assert_true(metadata->backup_history[0].id == 1);
  assert_true(metadata->backup_history[0].timestamp == 3487);
  assert_true(metadata->backup_history[0].ref_count == 2);

  assert_true(metadata->backup_history[1].id == 2);
  assert_true(metadata->backup_history[1].timestamp == 2645);
  assert_true(metadata->backup_history[1].ref_count == 0);

  assert_true(metadata->backup_history[2].id == 3);
  assert_true(metadata->backup_history[2].timestamp == 9742);
  assert_true(metadata->backup_history[2].ref_count == 3);

  mustHaveConf(metadata, 3, 210, (uint8_t *)"0cdef2019a2c1f8130eb");

  assert_true(metadata->total_path_count == 4);

  PathNode *home =
    findNode(metadata, metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, 3, 0, 0, 12878, 0755);

  PathNode *user = findNode(metadata, home->subnodes, "/home/user",
                            BPOL_mirror, 1, 2);
  mustHaveDirectory(user, 0, 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(metadata, user->subnodes,
                              "/home/user/.bashrc", BPOL_track, 3, 0);
  mustHaveRegular(bashrc, 3, 1000, 75, 9348, 0755, 252,
                  (uint8_t *)"cdef2019a2c1f8130eb0");
  mustHaveNonExisting(bashrc, 1);
  mustHaveRegular(bashrc, 0, 983, 57, 1920, 0655, 579,
                  (uint8_t *)"8130eb0cdef2019a2c1f");

  PathNode *config = findNode(metadata, user->subnodes,
                              "/home/user/.config", BPOL_track, 1, 0);
  mustHaveDirectory(config, 1, 783, 192, 3487901, 0575);
}

int main(void)
{
  testGroupStart("generating simple test metadata");
  checkTestData1(genTestData1());
  checkTestData2(genTestData2());
  testGroupEnd();
}
