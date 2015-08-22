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
    .type = PST_regular,
    .uid = uid,
    .gid = gid,
    .timestamp = timestamp,
    .metadata.dir_mode = mode
  };

  appendHist(node, backup_id, metadata, state);
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
  appendHistDirectory(portage, 4, metadata, 7,  19, 12837, 0666);
  appendHistDirectory(portage, 3, metadata, 89, 98, 91234, 0321);

  PathNode *make_conf =
    createPathNode("make.conf", BPOL_track, portage, metadata);

  appendHistSymlink(make_conf, 4, metadata, 59, 23, 1248,
                    "make.conf.backup");
  appendHistNonExisting(make_conf, 3, metadata);
  appendHistRegular(make_conf, 1, metadata, 3, 4, 53238, 0713, 192,
                    (uint8_t *)"e78863d5e021dd60c1a2");

  return metadata;
}

/** Checks a Metadata struct generated by genTestData1().

  @param metadata The struct which should be checked.
*/
static void checkTestData1(Metadata *metadata)
{
  assert_true(metadata != NULL);
  assert_true(strCompare(metadata->repo_path, str("foo")));

  assert_true(metadata->current_backup.id == 0);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(metadata->current_backup.ref_count == 0);

  assert_true(metadata->backup_history_length == 4);
  assert_true(metadata->backup_history != NULL);

  assert_true(metadata->backup_history[0].id == 1);
  assert_true(metadata->backup_history[0].timestamp == 1234);
  assert_true(metadata->backup_history[0].ref_count == 1);

  assert_true(metadata->backup_history[1].id == 2);
  assert_true(metadata->backup_history[1].timestamp == 4321);
  assert_true(metadata->backup_history[1].ref_count == 0);

  assert_true(metadata->backup_history[2].id == 3);
  assert_true(metadata->backup_history[2].timestamp == 7890);
  assert_true(metadata->backup_history[2].ref_count == 3);

  assert_true(metadata->backup_history[3].id == 4);
  assert_true(metadata->backup_history[3].timestamp == 9876);
  assert_true(metadata->backup_history[3].ref_count == 5);

  assert_true(metadata->total_path_count == 6);
  assert_true(metadata->paths != NULL);
}

int main(void)
{
  testGroupStart("generating simple test metadata");
  checkTestData1(genTestData1());
  testGroupEnd();
}
