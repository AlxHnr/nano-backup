/** @file
  Tests repository metadata handling.
*/

#include "metadata.h"

#include <stdlib.h>

#include "CRegion/global-region.h"

#include "error-handling.h"
#include "file-hash.h"
#include "memory-pool.h"
#include "safe-math.h"
#include "safe-wrappers.h"
#include "test-common.h"
#include "test.h"

/** Writes the given metadata to the temporary test directory.

  @param metadata The metadata which should be written.
*/
static void writeMetadataToTmpDir(Metadata *metadata)
{
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/metadata"));
}

/** Initializes a history point.

  @param metadata The metadata, containing the backup history array.
  @param index The index of the history point in the array, which should be
  initialized.
  @param id The backup id which should be assigned to the point.
  @param timestamp The timestamp to be assigned to the point.
*/
static void initHistPoint(Metadata *metadata, size_t index, size_t id, time_t timestamp)
{
  metadata->backup_history[index].id = id;
  metadata->backup_history[index].timestamp = timestamp;
  metadata->backup_history[index].ref_count = 0;
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
static PathNode *createPathNode(const char *path_str, BackupPolicy policy, PathNode *parent_node,
                                Metadata *metadata)
{
  PathNode *node = mpAlloc(sizeof *node);

  node->hint = BH_none;
  node->policy = policy;
  node->history = NULL;
  node->subnodes = NULL;

  if(parent_node == NULL)
  {
    String path = strAppendPath(strWrap(""), strWrap(path_str));
    memcpy(&node->path, &path, sizeof(node->path));

    node->next = NULL;
  }
  else
  {
    String path = strAppendPath(parent_node->path, strWrap(path_str));
    memcpy(&node->path, &path, sizeof(node->path));

    node->next = parent_node->subnodes;
    parent_node->subnodes = node;
  }

  strTableMap(metadata->path_table, node->path, node);
  metadata->total_path_count = sSizeAdd(metadata->total_path_count, 1);

  return node;
}

/** Wrapper around findPathNode(). It is almost identical, but doesn't take
  a PathNodeHint. */
static PathNode *findNode(PathNode *start_node, const char *path_str, BackupPolicy policy, size_t history_length,
                          size_t subnode_count)
{
  return findPathNode(start_node, path_str, BH_none, policy, history_length, subnode_count);
}

/** Appends a new history point to the given node.

  @param node The node to which the history point should be appended to.
  @param backup The backup, to which the history point belongs to.
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

  memcpy(&history_point->state, &state, sizeof(history_point->state));
  history_point->next = NULL;
}

/** Assigns the given values to the specified path state as defined in the
  documentation of RegularMetadata.

  @param state A path state which must have the type PST_regular.
  @param mode The permission bits of the file.
  @param timestamp The modification timestamp of the file.
  @param size The size of the file described by the path state.
  @param hash The hash of the file or the files entire content, depending
  on the files size.
  @param slot The slot number of the file in the repository. Will be
  ignored if the files size is not greater than FILE_HASH_SIZE.
*/
static void assignRegularValues(PathState *state, mode_t mode, time_t timestamp, uint64_t size, uint8_t *hash,
                                uint8_t slot)
{
  state->metadata.reg.mode = mode;
  state->metadata.reg.timestamp = timestamp;
  state->metadata.reg.size = size;

  if(size > FILE_HASH_SIZE)
  {
    memcpy(state->metadata.reg.hash, hash, FILE_HASH_SIZE);
    state->metadata.reg.slot = slot;
  }
  else if(size > 0)
  {
    memcpy(state->metadata.reg.hash, hash, size);
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
  Will be ignored if the file size is not bigger than FILE_HASH_SIZE.
*/
static void appendHistRegular(PathNode *node, Backup *backup, uid_t uid, gid_t gid, time_t timestamp, mode_t mode,
                              uint64_t size, uint8_t *hash, uint8_t slot)
{
  PathState state = {
    .type = PST_regular,
    .uid = uid,
    .gid = gid,
  };

  assignRegularValues(&state, mode, timestamp, size, hash, slot);
  appendHist(node, backup, state);
}

/** A wrapper around appendHist(), which appends the path state of a
  symbolic link to the given node. It is like appendHistRegular(), but
  takes the following additional arguments:

  @param sym_target The target path of the symlink. The created history
  point will keep a reference to this string, so make sure not to mutate it
  as long as the history point is in use.
*/
static void appendHistSymlink(PathNode *node, Backup *backup, uid_t uid, gid_t gid, const char *sym_target)
{
  PathState state = {
    .type = PST_symlink,
    .uid = uid,
    .gid = gid,
    .metadata.sym_target = strWrap(sym_target),
  };

  appendHist(node, backup, state);
}

/** Like appendHistRegular(), but for a directory. */
static void appendHistDirectory(PathNode *node, Backup *backup, uid_t uid, gid_t gid, time_t timestamp,
                                mode_t mode)
{
  PathState state = {
    .type = PST_directory,
    .uid = uid,
    .gid = gid,
  };

  state.metadata.dir.mode = mode;
  state.metadata.dir.timestamp = timestamp;
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
  Will be ignored if the file size is not greater than FILE_HASH_SIZE.
*/
static void appendConfHist(Metadata *metadata, Backup *backup, uint64_t size, uint8_t *hash, uint8_t slot)
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

  memset(&history_point->state, 0, sizeof(history_point->state));
  history_point->state.type = PST_regular;
  history_point->state.uid = 0;
  history_point->state.gid = 0;

  assignRegularValues(&history_point->state, 0, 0, size, hash, slot);
  history_point->next = NULL;
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
      mpAlloc(sSizeMul(sizeof *metadata->backup_history, metadata->backup_history_length));
  }

  metadata->config_history = NULL;
  metadata->total_path_count = 0;
  metadata->path_table = strTableNew(CR_GetGlobalRegion());
  metadata->paths = NULL;

  return metadata;
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

  appendConfHist(metadata, &metadata->backup_history[1], 131, (uint8_t *)"9a2c1f8130eb0cdef201", 0);
  appendConfHist(metadata, &metadata->backup_history[3], 21, (uint8_t *)"f8130eb0cdef2019a2c1", 98);

  PathNode *etc = createPathNode("etc", BPOL_none, NULL, metadata);
  appendHistDirectory(etc, &metadata->backup_history[3], 12, 8, INT32_MAX, 0777);
  metadata->paths = etc;

  PathNode *conf_d = createPathNode("conf.d", BPOL_none, etc, metadata);
  appendHistDirectory(conf_d, &metadata->backup_history[3], 3, 5, 102934, 0123);

  appendHistRegular(createPathNode("foo", BPOL_mirror, conf_d, metadata), &metadata->backup_history[3], 91, 47,
                    680123, 0223, 20, (uint8_t *)"66f69cd1998e54ae5533", 122);

  appendHistRegular(createPathNode("bar", BPOL_mirror, conf_d, metadata), &metadata->backup_history[2], 89, 20,
                    310487, 0523, 48, (uint8_t *)"fffffcd1998e54ae5a70", 12);

  PathNode *portage = createPathNode("portage", BPOL_track, etc, metadata);
  appendHistDirectory(portage, &metadata->backup_history[2], 89, 98, 91234, 0321);
  appendHistDirectory(portage, &metadata->backup_history[3], 7, 19, 12837, 0666);

  PathNode *make_conf = createPathNode("make.conf", BPOL_track, portage, metadata);

  appendHistSymlink(make_conf, &metadata->backup_history[0], 59, 23, "make.conf.backup");
  appendHistNonExisting(make_conf, &metadata->backup_history[2]);
  appendHistRegular(make_conf, &metadata->backup_history[3], 3, 4, 53238, 0713, 192,
                    (uint8_t *)"e78863d5e021dd60c1a2", 0);

  return metadata;
}

/** Checks a Metadata struct generated by genTestData1().

  @param metadata The struct which should be checked.
*/
static void checkTestData1(Metadata *metadata)
{
  checkMetadata(metadata, 2, true);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 4);

  checkHistPoint(metadata, 0, 0, 1234, 1);
  checkHistPoint(metadata, 1, 1, -1334953412, 1);
  checkHistPoint(metadata, 2, 2, 7890, 3);
  checkHistPoint(metadata, 3, 3, 9876, 6);

  mustHaveConf(metadata, &metadata->backup_history[1], 131, (uint8_t *)"9a2c1f8130eb0cdef201", 0);
  mustHaveConf(metadata, &metadata->backup_history[3], 21, (uint8_t *)"f8130eb0cdef2019a2c1", 98);

  assert_true(metadata->total_path_count == 6);

  PathNode *etc = findNode(metadata->paths, "/etc", BPOL_none, 1, 2);
  mustHaveDirectory(etc, &metadata->backup_history[3], 12, 8, INT32_MAX, 0777);

  PathNode *conf_d = findNode(etc->subnodes, "/etc/conf.d", BPOL_none, 1, 2);
  mustHaveDirectory(conf_d, &metadata->backup_history[3], 3, 5, 102934, 0123);

  PathNode *foo = findNode(conf_d->subnodes, "/etc/conf.d/foo", BPOL_mirror, 1, 0);
  mustHaveRegular(foo, &metadata->backup_history[3], 91, 47, 680123, 0223, 20, (uint8_t *)"66f69cd1998e54ae5533",
                  48);

  PathNode *bar = findNode(conf_d->subnodes, "/etc/conf.d/bar", BPOL_mirror, 1, 0);
  mustHaveRegular(bar, &metadata->backup_history[2], 89, 20, 310487, 0523, 48, (uint8_t *)"fffffcd1998e54ae5a70",
                  12);

  PathNode *portage = findNode(etc->subnodes, "/etc/portage", BPOL_track, 2, 1);
  mustHaveDirectory(portage, &metadata->backup_history[2], 89, 98, 91234, 0321);
  mustHaveDirectory(portage, &metadata->backup_history[3], 7, 19, 12837, 0666);

  PathNode *make_conf = findNode(portage->subnodes, "/etc/portage/make.conf", BPOL_track, 3, 0);
  mustHaveSymlink(make_conf, &metadata->backup_history[0], 59, 23, "make.conf.backup");
  mustHaveNonExisting(make_conf, &metadata->backup_history[2]);
  mustHaveRegular(make_conf, &metadata->backup_history[3], 3, 4, 53238, 0713, 192,
                  (uint8_t *)"e78863d5e021dd60c1a2", 0);
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

  appendConfHist(metadata, &metadata->backup_history[2], 210, (uint8_t *)"0cdef2019a2c1f8130eb", 255);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->backup_history[2], 0, 0, 12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistRegular(bashrc, &metadata->backup_history[0], 983, 57, 1920, 0655, 1,
                    (uint8_t *)"8130eb0cdef2019a2c1f", 255);
  appendHistNonExisting(bashrc, &metadata->backup_history[1]);
  appendHistRegular(bashrc, &metadata->backup_history[2], 1000, 75, 9348, 0755, 252,
                    (uint8_t *)"cdef2019a2c1f8130eb0", 43);

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
  checkMetadata(metadata, 1, true);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);

  checkHistPoint(metadata, 0, 0, 3487, 4);
  checkHistPoint(metadata, 1, 1, 2645, 2);
  checkHistPoint(metadata, 2, 2, 9742, 3);

  mustHaveConf(metadata, &metadata->backup_history[2], 210, (uint8_t *)"0cdef2019a2c1f8130eb", 255);

  assert_true(metadata->total_path_count == 5);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[2], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 2);
  mustHaveDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 3, 0);
  mustHaveRegular(bashrc, &metadata->backup_history[0], 983, 57, 1920, 0655, 1, (uint8_t *)"8???????????????????",
                  19);
  mustHaveNonExisting(bashrc, &metadata->backup_history[1]);
  mustHaveRegular(bashrc, &metadata->backup_history[2], 1000, 75, 9348, 0755, 252,
                  (uint8_t *)"cdef2019a2c1f8130eb0", 43);

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

  appendConfHist(metadata, &metadata->backup_history[1], 3, (uint8_t *)"fbc92e19ee0cd2140faa", 0);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->backup_history[1], 0, 0, 12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->backup_history[3], 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920, 0655, 0,
                    (uint8_t *)"8130eb0cdef2019a2c1f", 1);
  appendHistNonExisting(bashrc, &metadata->backup_history[4]);

  PathNode *config = createPathNode(".config", BPOL_track, user, metadata);
  appendHistDirectory(config, &metadata->backup_history[4], 783, 192, 3487901, 0575);

  return metadata;
}

/** Tests a tree generated by genUnusedBackupPoints(), which was written
  and reloaded from disk. */
static void checkLoadedUnusedBackupPoints(Metadata *metadata)
{
  checkMetadata(metadata, 1, true);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);

  checkHistPoint(metadata, 0, 0, 140908, 3);
  checkHistPoint(metadata, 1, 1, -6810, 1);
  checkHistPoint(metadata, 2, 2, 54111, 2);

  mustHaveConf(metadata, &metadata->backup_history[0], 3, (uint8_t *)"fbc?????????????????", 73);

  assert_true(metadata->total_path_count == 4);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[0], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 2);
  mustHaveDirectory(user, &metadata->backup_history[1], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 2, 0);
  mustHaveRegular(bashrc, &metadata->backup_history[0], 983, 57, 1920, 0655, 0, (uint8_t *)"xxxxxxxxxxxxxxxxxxxx",
                  27);
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

  appendConfHist(metadata, &metadata->current_backup, 6723, (uint8_t *)"fbc92e19ee0cd2140faa", 76);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->backup_history[0], 0, 0, 12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->current_backup, 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistNonExisting(bashrc, &metadata->current_backup);
  appendHistRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920, 0655, 7,
                    (uint8_t *)"8130eb0cdef2019a2c1f", 8);

  return metadata;
}

/** Checks the given metadata struct, which was generated by
  genCurrentBackupData() and then reloaded from disk. */
static void checkLoadedCurrentBackupData(Metadata *metadata)
{
  checkMetadata(metadata, 1, true);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);

  checkHistPoint(metadata, 0, 0, 57645, 3);
  checkHistPoint(metadata, 1, 1, 48390, 1);
  checkHistPoint(metadata, 2, 2, 84908, 1);

  mustHaveConf(metadata, &metadata->backup_history[0], 6723, (uint8_t *)"fbc92e19ee0cd2140faa", 76);

  assert_true(metadata->total_path_count == 3);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[1], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 1);
  mustHaveDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 2, 0);
  mustHaveNonExisting(bashrc, &metadata->backup_history[0]);
  mustHaveRegular(bashrc, &metadata->backup_history[2], 983, 57, 1920, 0655, 7, (uint8_t *)"8130eb0-------------",
                  0);
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
  appendHistDirectory(home, &metadata->backup_history[0], 0, 0, 12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->backup_history[2], 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistNonExisting(bashrc, &metadata->backup_history[0]);
  appendHistRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920, 0655, 579,
                    (uint8_t *)"8130eb0cdef2019a2c1f", 128);

  return metadata;
}

/** Checks the metadata generated by genNoConfHist(). */
static void checkNoConfHist(Metadata *metadata)
{
  checkMetadata(metadata, 0, true);
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
  mustHaveRegular(bashrc, &metadata->backup_history[1], 983, 57, 1920, 0655, 579,
                  (uint8_t *)"8130eb0cdef2019a2c1f", 128);
}

/** Generates a dummy metadata struct with no path tree. It can be checked
  with checkNoPathTree(). */
static Metadata *genNoPathTree(void)
{
  Metadata *metadata = createEmptyMetadata(2);
  initHistPoint(metadata, 0, 0, 3249);
  initHistPoint(metadata, 1, 1, 29849483);

  appendConfHist(metadata, &metadata->backup_history[0], 19, (uint8_t *)"fbc92e19ee0cd2140faa", 34);
  appendConfHist(metadata, &metadata->backup_history[1], 103894, (uint8_t *)"some test bytes?????", 35);

  return metadata;
}

/** Checks the metadata generated by genNoPathTree(). */
static void checkNoPathTree(Metadata *metadata)
{
  checkMetadata(metadata, 2, true);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 2);

  checkHistPoint(metadata, 0, 0, 3249, 1);
  checkHistPoint(metadata, 1, 1, 29849483, 1);

  mustHaveConf(metadata, &metadata->backup_history[0], 19, (uint8_t *)"fbc92e19ee0cd2140fa%", 8);
  mustHaveConf(metadata, &metadata->backup_history[1], 103894, (uint8_t *)"some test bytes?????", 35);

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
  checkMetadata(metadata, 0, true);
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

  appendConfHist(metadata, &metadata->current_backup, 6723, (uint8_t *)"fbc92e19ee0cd2140faa", 1);

  PathNode *home = createPathNode("home", BPOL_none, NULL, metadata);
  appendHistDirectory(home, &metadata->current_backup, 0, 0, 12878, 0755);
  metadata->paths = home;

  PathNode *user = createPathNode("user", BPOL_mirror, home, metadata);
  appendHistDirectory(user, &metadata->current_backup, 1000, 75, 120948, 0600);

  PathNode *bashrc = createPathNode(".bashrc", BPOL_track, user, metadata);
  appendHistRegular(bashrc, &metadata->current_backup, 983, 57, -1, 0655, 0, (uint8_t *)"8130eb0cdef2019a2c1f",
                    127);

  return metadata;
}

/** Counterpart to initOnlyCurrentBackupData(). */
static void checkOnlyCurrentBackupData(Metadata *metadata)
{
  checkMetadata(metadata, 1, true);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 1);

  checkHistPoint(metadata, 0, 0, 1348981, 4);

  mustHaveConf(metadata, &metadata->backup_history[0], 6723, (uint8_t *)"fbc92e19ee0cd2140faa", 1);

  assert_true(metadata->total_path_count == 3);

  PathNode *home = findNode(metadata->paths, "/home", BPOL_none, 1, 1);
  mustHaveDirectory(home, &metadata->backup_history[0], 0, 0, 12878, 0755);

  PathNode *user = findNode(home->subnodes, "/home/user", BPOL_mirror, 1, 1);
  mustHaveDirectory(user, &metadata->backup_history[0], 1000, 75, 120948, 0600);

  PathNode *bashrc = findNode(user->subnodes, "/home/user/.bashrc", BPOL_track, 1, 0);
  mustHaveRegular(bashrc, &metadata->backup_history[0], 983, 57, -1, 0655, 0, (uint8_t *)"....................",
                  217);
}

/** Generates a metadata tree containing various nodes which are not part
  of the repository anymore. After reloading this tree from disk, it can be
  checked via checkWipedNodes(). */
static Metadata *genNodesToWipe(void)
{
  Metadata *metadata = createEmptyMetadata(4);
  initHistPoint(metadata, 0, 0, 1234);
  initHistPoint(metadata, 1, 1, -1334953412);
  initHistPoint(metadata, 2, 2, 7890);
  initHistPoint(metadata, 3, 3, 9876);

  appendConfHist(metadata, &metadata->backup_history[1], 131, (uint8_t *)"9a2c1f8130eb0cdef201", 0);
  appendConfHist(metadata, &metadata->backup_history[3], 21, (uint8_t *)"f8130eb0cdef2019a2c1", 98);

  PathNode *etc = createPathNode("etc", BPOL_none, NULL, metadata);
  appendHistDirectory(etc, &metadata->backup_history[3], 12, 8, INT32_MAX, 0777);
  metadata->paths = etc;

  PathNode *conf_d = createPathNode("conf.d", BPOL_none, etc, metadata);
  appendHistDirectory(conf_d, &metadata->backup_history[3], 3, 5, 102934, 0123);
  appendHistRegular(createPathNode("foo", BPOL_mirror, conf_d, metadata), &metadata->backup_history[3], 91, 47,
                    680123, 0223, 20, (uint8_t *)"66f69cd1998e54ae5533", 122);
  appendHistRegular(createPathNode("bar", BPOL_mirror, conf_d, metadata), &metadata->backup_history[2], 89, 20,
                    310487, 0523, 48, (uint8_t *)"fffffcd1998e54ae5a70", 12);

  PathNode *portage = createPathNode("portage", BPOL_track, etc, metadata);
  appendHistDirectory(portage, &metadata->backup_history[2], 89, 98, 91234, 0321);
  appendHistDirectory(portage, &metadata->backup_history[3], 7, 19, 12837, 0666);
  PathNode *make_conf = createPathNode("make.conf", BPOL_track, portage, metadata);
  appendHistSymlink(make_conf, &metadata->backup_history[0], 59, 23, "make.conf.backup");
  appendHistNonExisting(make_conf, &metadata->backup_history[2]);
  appendHistRegular(make_conf, &metadata->backup_history[3], 3, 4, 53238, 0713, 192,
                    (uint8_t *)"e78863d5e021dd60c1a2", 0);
  PathNode *package_use = createPathNode("package.use", BPOL_copy, portage, metadata);
  appendHistDirectory(package_use, &metadata->backup_history[3], 34, 25, 184912, 0754);
  appendHistSymlink(createPathNode("packages", BPOL_mirror, package_use, metadata), &metadata->backup_history[1],
                    32, 28, "../packages.txt");

  /* Decrement wiped nodes reference count. */
  conf_d->hint = BH_not_part_of_repository;
  make_conf->hint = BH_not_part_of_repository | BH_policy_changed | BH_loses_history;
  metadata->backup_history[3].ref_count -= 3;
  metadata->backup_history[2].ref_count -= 2;
  metadata->backup_history[0].ref_count -= 1;
  metadata->total_path_count -= 4;

  return metadata;
}

/** Checks if certain nodes got wiped properly from the tree generated via
  genNodesToWipe(). */
static void checkWipedNodes(Metadata *metadata)
{
  checkMetadata(metadata, 2, true);
  assert_true(metadata->current_backup.ref_count == 0);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == 4);

  checkHistPoint(metadata, 0, 0, -1334953412, 2);
  checkHistPoint(metadata, 1, 1, 7890, 1);
  checkHistPoint(metadata, 2, 2, 9876, 4);

  mustHaveConf(metadata, &metadata->backup_history[0], 131, (uint8_t *)"9a2c1f8130eb0cdef201", 0);
  mustHaveConf(metadata, &metadata->backup_history[2], 21, (uint8_t *)"f8130eb0cdef2019a2c1", 98);

  PathNode *etc = findNode(metadata->paths, "/etc", BPOL_none, 1, 1);
  mustHaveDirectory(etc, &metadata->backup_history[2], 12, 8, INT32_MAX, 0777);

  PathNode *portage = findNode(etc->subnodes, "/etc/portage", BPOL_track, 2, 1);
  mustHaveDirectory(portage, &metadata->backup_history[1], 89, 98, 91234, 0321);
  mustHaveDirectory(portage, &metadata->backup_history[2], 7, 19, 12837, 0666);

  PathNode *package_use = findNode(portage->subnodes, "/etc/portage/package.use", BPOL_copy, 1, 1);
  mustHaveDirectory(package_use, &metadata->backup_history[2], 34, 25, 184912, 0754);
  mustHaveSymlink(findNode(package_use->subnodes, "/etc/portage/package.use/packages", BPOL_mirror, 1, 0),
                  &metadata->backup_history[0], 32, 28, "../packages.txt");
}

/** Combines sFopenWrite(), sFwrite() and sFclose. */
static void writeBytesToFile(size_t size, const char *data, const char *path)
{
  FileStream *writer = sFopenWrite(strWrap(path));
  sFwrite(data, size, writer);
  sFclose(writer);
}

/** Replaces the -3th byte in the given string, truncates it to length -2,
  writes the metadata and undoes the changes.

  @param metadata The metadata to write.
  @param path The path to manipulate.
  @param byte The character which should replace the old one.
  @param filename The name of the final metadata file.
*/
static void writeWithBrokenChar3(Metadata *metadata, String *path, char byte, const char *filename)
{
  const char old_byte = path->content[path->length - 3];

  /* To generate broken metadata at runtime it is required to overwrite
     const data. This data is allocated on the heap and can be overwritten
     safely. */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
  *((char *)&path->content[path->length - 3]) = byte;
  *((size_t *)&path->length) -= 2;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap(filename));
  *((size_t *)&path->length) += 2;
  *((char *)&path->content[path->length - 3]) = old_byte;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}

/** Searches for a string in the given data.

  @param data The data to search.
  @param string The string to find.
  @param data_length The length of data.

  @return The address of the first byte of `string`.
*/
static char *findString(char *data, const char *string, size_t data_length)
{
  const size_t string_length = strlen(string);
  assert_true(string_length > 0);
  assert_true(string_length < data_length);

  const size_t max = data_length - string_length;
  for(size_t index = 0; index < max; index++)
  {
    if(memcmp(&data[index], string, string_length) == 0)
    {
      return &data[index];
    }
  }

  die("unable to find string in memory: \"%s\"", string);
}

/** Copies the given string into data without the terminating null-byte. */
static void copyStringRaw(char *data, const char *string)
{
  /* NOLINTNEXTLINE(bugprone-not-null-terminated-result) */
  memcpy(data, string, strlen(string));
}

/** Generates various broken metadata files. */
static void generateBrokenMetadata(void)
{
  metadataWrite(genTestData1(), strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/test-data-1"));
  CR_Region *r = CR_RegionNew();
  char *test_data = sGetFilesContent(r, strWrap("tmp/test-data-1")).content;

  Metadata *metadata = metadataLoad(strWrap("tmp/test-data-1"));
  checkMetadata(metadata, 2, true);
  PathNode *portage = strTableGet(metadata->path_table, strWrap("/etc/portage"));
  assert_true(portage != NULL);

  /* Truncate metadata file to provoke errors. */
  writeBytesToFile(643, test_data, "tmp/missing-byte");
  writeBytesToFile(606, test_data, "tmp/missing-slot");
  writeBytesToFile(402, test_data, "tmp/missing-path-state-type");
  writeBytesToFile(647, test_data, "tmp/incomplete-32-bit-value");
  writeBytesToFile(217, test_data, "tmp/missing-32-bit-value");
  writeBytesToFile(3, test_data, "tmp/incomplete-size");
  writeBytesToFile(327, test_data, "tmp/missing-size");
  writeBytesToFile(520, test_data, "tmp/incomplete-time");
  writeBytesToFile(656, test_data, "tmp/missing-time");
  writeBytesToFile(148, test_data, "tmp/incomplete-hash");
  writeBytesToFile(85, test_data, "tmp/missing-hash");
  writeBytesToFile(249, test_data, "tmp/incomplete-path");
  writeBytesToFile(188, test_data, "tmp/missing-path");
  writeBytesToFile(384, test_data, "tmp/incomplete-symlink-target-path");
  writeBytesToFile(378, test_data, "tmp/missing-symlink-target-path");
  writeBytesToFile(699, test_data, "tmp/last-byte-missing");

  /* Generate files with out-of-range backup IDs. */
  Backup broken_backup = { .id = 4 };

  Backup *old_backup = metadata->config_history->next->backup;
  metadata->config_history->next->backup = &broken_backup;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/backup-id-out-of-range-1"));
  metadata->config_history->next->backup = old_backup;

  old_backup = portage->history->backup;
  portage->history->backup = &broken_backup;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/backup-id-out-of-range-2"));
  broken_backup.id = 19;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/backup-id-out-of-range-3"));
  portage->history->backup = old_backup;

  /* Generate file with invalid path states. */
  portage->history->next->state.type = 4;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/invalid-path-state-type"));
  portage->history->next->state.type = PST_directory;

  /* Generate file with unneeded trailing bytes. */
  FileStream *stream = sFopenWrite(strWrap("tmp/unneeded-trailing-bytes"));
  sFwrite(test_data, 700, stream);
  sFwrite("   ", 3, stream);
  sFclose(stream);

  test_data[172] = 0;
  writeBytesToFile(700, test_data, "tmp/path-count-zero");
  test_data[172] = 1;

  /* Generate metadata containing zero-length filenames. */
  PathNode *etc = strTableGet(metadata->path_table, strWrap("/etc"));
  assert_true(etc != NULL);
  PathNode *conf_d = strTableGet(metadata->path_table, strWrap("/etc/conf.d"));
  assert_true(conf_d != NULL);
  PathNode *foo = strTableGet(metadata->path_table, strWrap("/etc/conf.d/foo"));
  assert_true(foo != NULL);
  PathNode *bar = strTableGet(metadata->path_table, strWrap("/etc/conf.d/bar"));
  assert_true(bar != NULL);

  /* To generate broken metadata at runtime it is required to overwrite
     const data. This data is allocated on the heap and can be overwritten
     safely. */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
  *((size_t *)&etc->path.length) -= 3;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/filename-with-length-zero-1"));
  *((size_t *)&etc->path.length) += 3;

  *((size_t *)&foo->path.length) -= 3;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/filename-with-length-zero-2"));
  *((size_t *)&foo->path.length) += 3;

  /* Generate metadata containing dot filenames. */
  *((char *)&conf_d->path.content[conf_d->path.length - 6]) = '.';
  *((char *)&conf_d->path.content[conf_d->path.length - 5]) = '.';
  *((size_t *)&conf_d->path.length) -= 4;
  metadataWrite(metadata, strWrap("tmp"), strWrap("tmp/tmp-file"), strWrap("tmp/dot-filename-2"));
  *((size_t *)&conf_d->path.length) += 4;
  *((char *)&conf_d->path.content[conf_d->path.length - 5]) = 'o';
  *((char *)&conf_d->path.content[conf_d->path.length - 6]) = 'c';
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

  writeWithBrokenChar3(metadata, &etc->path, '.', "tmp/dot-filename-1");
  writeWithBrokenChar3(metadata, &bar->path, '.', "tmp/dot-filename-3");

  /* Generate metadata containing slashes in filenames. */
  char *conf_d_bytes = findString(test_data, "conf.d", 700);
  char *portage_bytes = findString(test_data, "portage", 700);
  char *make_conf_bytes = findString(test_data, "make.conf", 700);
  writeWithBrokenChar3(metadata, &bar->path, '/', "tmp/slash-filename-1");

  conf_d_bytes[0] = '/';
  writeBytesToFile(700, test_data, "tmp/slash-filename-2");
  copyStringRaw(conf_d_bytes, "conf.d");

  portage_bytes[2] = '/';
  writeBytesToFile(700, test_data, "tmp/slash-filename-3");
  portage_bytes[4] = '/';
  writeBytesToFile(700, test_data, "tmp/slash-filename-4");
  portage_bytes[6] = '/';
  writeBytesToFile(700, test_data, "tmp/slash-filename-5");
  portage_bytes[3] = '/';
  writeBytesToFile(700, test_data, "tmp/slash-filename-6");
  copyStringRaw(portage_bytes, "portage");

  make_conf_bytes[8] = '/';
  writeBytesToFile(700, test_data, "tmp/slash-filename-7");
  copyStringRaw(make_conf_bytes, "make.conf");

  /* Generate metadata containing null-bytes in filenames. */
  writeWithBrokenChar3(metadata, &foo->path, '\0', "tmp/null-byte-filename-1");
  writeWithBrokenChar3(metadata, &conf_d->path, '\0', "tmp/null-byte-filename-2");

  portage_bytes[2] = '\0';
  writeBytesToFile(700, test_data, "tmp/null-byte-filename-3");
  portage_bytes[4] = '\0';
  writeBytesToFile(700, test_data, "tmp/null-byte-filename-4");
  portage_bytes[3] = '\0';
  writeBytesToFile(700, test_data, "tmp/null-byte-filename-5");
  copyStringRaw(portage_bytes, "portage");

  make_conf_bytes[0] = '\0';
  writeBytesToFile(700, test_data, "tmp/null-byte-filename-6");
  copyStringRaw(make_conf_bytes, "make.conf");

  make_conf_bytes[8] = '\0';
  writeBytesToFile(700, test_data, "tmp/null-byte-filename-7");
  copyStringRaw(make_conf_bytes, "make.conf");

  /* Generate metadata with both slashes and null-bytes in filenames. */
  conf_d_bytes[2] = '\0';
  conf_d_bytes[4] = '/';
  writeBytesToFile(700, test_data, "tmp/slash-and-null-byte-filename-1");
  copyStringRaw(conf_d_bytes, "conf.d");

  portage_bytes[2] = '/';
  portage_bytes[6] = '\0';
  writeBytesToFile(700, test_data, "tmp/slash-and-null-byte-filename-2");
  copyStringRaw(portage_bytes, "portage");

  make_conf_bytes[0] = '\0';
  make_conf_bytes[1] = '/';
  make_conf_bytes[2] = '/';
  make_conf_bytes[5] = '\0';
  writeBytesToFile(700, test_data, "tmp/slash-and-null-byte-filename-3");
  copyStringRaw(make_conf_bytes, "make.conf");

  /* Assert that all bytes got reset properly. */
  checkTestData1(metadata);

  writeBytesToFile(700, test_data, "tmp/test-data-1");
  checkTestData1(metadataLoad(strWrap("tmp/test-data-1")));

  CR_RegionRelease(r);
}

/** Tests detection of corruption in metadata. */
static void testRejectingCorruptedMetadata(void)
{
  generateBrokenMetadata();
  assert_error_errno(metadataLoad(strWrap("non-existing.txt")), "failed to access \"non-existing.txt\"", ENOENT);
  assert_error(metadataLoad(strWrap("tmp/missing-byte")),
               "corrupted metadata: expected 1 byte, got 0: \"tmp/missing-byte\"");
  assert_error(metadataLoad(strWrap("tmp/missing-slot")),
               "corrupted metadata: expected 1 byte, got 0: \"tmp/missing-slot\"");
  assert_error(metadataLoad(strWrap("tmp/invalid-path-state-type")),
               "invalid PathStateType in \"tmp/invalid-path-state-type\"");
  assert_error(metadataLoad(strWrap("tmp/missing-path-state-type")),
               "corrupted metadata: expected 1 byte, got 0: \"tmp/missing-path-state-type\"");
  assert_error(metadataLoad(strWrap("tmp/incomplete-32-bit-value")),
               "corrupted metadata: expected 4 bytes, got 3: \"tmp/incomplete-32-bit-value\"");
  assert_error(metadataLoad(strWrap("tmp/missing-32-bit-value")),
               "corrupted metadata: expected 4 bytes, got 0: \"tmp/missing-32-bit-value\"");
  assert_error(metadataLoad(strWrap("tmp/incomplete-size")),
               "corrupted metadata: expected 8 bytes, got 3: \"tmp/incomplete-size\"");
  assert_error(metadataLoad(strWrap("tmp/missing-size")),
               "corrupted metadata: expected 8 bytes, got 0: \"tmp/missing-size\"");
  assert_error(metadataLoad(strWrap("tmp/backup-id-out-of-range-1")),
               "backup id is out of range in \"tmp/backup-id-out-of-range-1\"");
  assert_error(metadataLoad(strWrap("tmp/backup-id-out-of-range-2")),
               "backup id is out of range in \"tmp/backup-id-out-of-range-2\"");
  assert_error(metadataLoad(strWrap("tmp/backup-id-out-of-range-3")),
               "backup id is out of range in \"tmp/backup-id-out-of-range-3\"");
  assert_error(metadataLoad(strWrap("tmp/incomplete-time")),
               "corrupted metadata: expected 8 bytes, got 7: \"tmp/incomplete-time\"");
  assert_error(metadataLoad(strWrap("tmp/missing-time")),
               "corrupted metadata: expected 8 bytes, got 0: \"tmp/missing-time\"");
  assert_error(metadataLoad(strWrap("tmp/incomplete-hash")),
               "corrupted metadata: expected 20 bytes, got 5: \"tmp/incomplete-hash\"");
  assert_error(metadataLoad(strWrap("tmp/missing-hash")),
               "corrupted metadata: expected 20 bytes, got 0: \"tmp/missing-hash\"");
  assert_error(metadataLoad(strWrap("tmp/incomplete-path")),
               "corrupted metadata: expected 7 bytes, got 4: \"tmp/incomplete-path\"");
  assert_error(metadataLoad(strWrap("tmp/missing-path")),
               "corrupted metadata: expected 3 bytes, got 0: \"tmp/missing-path\"");
  assert_error(metadataLoad(strWrap("tmp/incomplete-symlink-target-path")),
               "corrupted metadata: expected 16 bytes, got 6: \"tmp/incomplete-symlink-target-path\"");
  assert_error(metadataLoad(strWrap("tmp/missing-symlink-target-path")),
               "corrupted metadata: expected 16 bytes, got 0: \"tmp/missing-symlink-target-path\"");
  assert_error(metadataLoad(strWrap("tmp/last-byte-missing")),
               "corrupted metadata: expected 8 bytes, got 7: \"tmp/last-byte-missing\"");
  assert_error(metadataLoad(strWrap("tmp/unneeded-trailing-bytes")),
               "unneeded trailing bytes in \"tmp/unneeded-trailing-bytes\"");
  assert_error(metadataLoad(strWrap("tmp/path-count-zero")), "unneeded trailing bytes in \"tmp/path-count-zero\"");

  assert_error(metadataLoad(strWrap("tmp/filename-with-length-zero-1")),
               "contains filename with length zero: \"tmp/filename-with-length-zero-1\"");
  assert_error(metadataLoad(strWrap("tmp/filename-with-length-zero-2")),
               "contains filename with length zero: \"tmp/filename-with-length-zero-2\"");

  assert_error(metadataLoad(strWrap("tmp/dot-filename-1")),
               "contains invalid filename \".\": \"tmp/dot-filename-1\"");
  assert_error(metadataLoad(strWrap("tmp/dot-filename-2")),
               "contains invalid filename \"..\": \"tmp/dot-filename-2\"");
  assert_error(metadataLoad(strWrap("tmp/dot-filename-3")),
               "contains invalid filename \".\": \"tmp/dot-filename-3\"");

  assert_error(metadataLoad(strWrap("tmp/slash-filename-1")),
               "contains invalid filename \"/\": \"tmp/slash-filename-1\"");
  assert_error(metadataLoad(strWrap("tmp/slash-filename-2")),
               "contains invalid filename \"/onf.d\": \"tmp/slash-filename-2\"");
  assert_error(metadataLoad(strWrap("tmp/slash-filename-3")),
               "contains invalid filename \"po/tage\": \"tmp/slash-filename-3\"");
  assert_error(metadataLoad(strWrap("tmp/slash-filename-4")),
               "contains invalid filename \"po/t/ge\": \"tmp/slash-filename-4\"");
  assert_error(metadataLoad(strWrap("tmp/slash-filename-5")),
               "contains invalid filename \"po/t/g/\": \"tmp/slash-filename-5\"");
  assert_error(metadataLoad(strWrap("tmp/slash-filename-6")),
               "contains invalid filename \"po///g/\": \"tmp/slash-filename-6\"");
  assert_error(metadataLoad(strWrap("tmp/slash-filename-7")),
               "contains invalid filename \"make.con/\": \"tmp/slash-filename-7\"");

  assert_error(metadataLoad(strWrap("tmp/null-byte-filename-1")),
               "contains filename with null-bytes: \"tmp/null-byte-filename-1\"");
  assert_error(metadataLoad(strWrap("tmp/null-byte-filename-2")),
               "contains filename with null-bytes: \"tmp/null-byte-filename-2\"");
  assert_error(metadataLoad(strWrap("tmp/null-byte-filename-3")),
               "contains filename with null-bytes: \"tmp/null-byte-filename-3\"");
  assert_error(metadataLoad(strWrap("tmp/null-byte-filename-4")),
               "contains filename with null-bytes: \"tmp/null-byte-filename-4\"");
  assert_error(metadataLoad(strWrap("tmp/null-byte-filename-5")),
               "contains filename with null-bytes: \"tmp/null-byte-filename-5\"");
  assert_error(metadataLoad(strWrap("tmp/null-byte-filename-6")),
               "contains filename with null-bytes: \"tmp/null-byte-filename-6\"");
  assert_error(metadataLoad(strWrap("tmp/null-byte-filename-7")),
               "contains filename with null-bytes: \"tmp/null-byte-filename-7\"");

  assert_error(metadataLoad(strWrap("tmp/slash-and-null-byte-filename-1")),
               "contains filename with null-bytes: \"tmp/slash-and-null-byte-filename-1\"");
  assert_error(metadataLoad(strWrap("tmp/slash-and-null-byte-filename-2")),
               "contains filename with null-bytes: \"tmp/slash-and-null-byte-filename-2\"");
  assert_error(metadataLoad(strWrap("tmp/slash-and-null-byte-filename-3")),
               "contains filename with null-bytes: \"tmp/slash-and-null-byte-filename-3\"");
}

int main(void)
{
  testGroupStart("metadataNew()");
  checkEmptyMetadata(metadataNew());
  testGroupEnd();

  testGroupStart("reading and writing of metadata");
  /* Write and read TestData1. */
  Metadata *test_data_1 = genTestData1();
  checkTestData1(test_data_1);

  writeMetadataToTmpDir(test_data_1);
  checkTestData1(metadataLoad(strWrap("tmp/metadata")));

  /* Write and read TestData2. */
  Metadata *test_data_2 = genTestData2();
  checkTestData2(test_data_2);

  writeMetadataToTmpDir(test_data_2);
  checkTestData2(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("writing only referenced backup points");
  Metadata *unused_backup_points = genUnusedBackupPoints();
  writeMetadataToTmpDir(unused_backup_points);
  checkLoadedUnusedBackupPoints(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("merging current backup point while writing");
  Metadata *current_backup_data = genCurrentBackupData();
  writeMetadataToTmpDir(current_backup_data);
  checkLoadedCurrentBackupData(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("adjust backup ID order");
  test_data_1->backup_history[0].id = 3;
  test_data_1->backup_history[1].id = 2;
  test_data_1->backup_history[2].id = 1;
  test_data_1->backup_history[3].id = 0;
  writeMetadataToTmpDir(test_data_1);
  checkTestData1(metadataLoad(strWrap("tmp/metadata")));

  test_data_1->backup_history[0].id = 12;
  test_data_1->backup_history[1].id = 8;
  test_data_1->backup_history[2].id = 12983948;
  test_data_1->backup_history[3].id = 0;
  writeMetadataToTmpDir(test_data_1);
  checkTestData1(metadataLoad(strWrap("tmp/metadata")));

  test_data_2->backup_history[0].id = 0;
  test_data_2->backup_history[1].id = 0;
  test_data_2->backup_history[2].id = 0;
  writeMetadataToTmpDir(test_data_2);
  checkTestData2(metadataLoad(strWrap("tmp/metadata")));

  unused_backup_points->backup_history[0].id = 0;
  unused_backup_points->backup_history[1].id = 35;
  unused_backup_points->backup_history[2].id = 982;
  unused_backup_points->backup_history[3].id = 982;
  unused_backup_points->backup_history[4].id = 5;
  unused_backup_points->backup_history[5].id = 0;
  writeMetadataToTmpDir(unused_backup_points);
  checkLoadedUnusedBackupPoints(metadataLoad(strWrap("tmp/metadata")));

  current_backup_data->backup_history[0].id = 70;
  current_backup_data->backup_history[1].id = 70;
  writeMetadataToTmpDir(current_backup_data);
  checkLoadedCurrentBackupData(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("no config history");
  Metadata *no_conf_hist = genNoConfHist();
  checkNoConfHist(no_conf_hist);
  writeMetadataToTmpDir(no_conf_hist);
  checkNoConfHist(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("no path tree");
  Metadata *no_path_tree = genNoPathTree();
  checkNoPathTree(no_path_tree);
  writeMetadataToTmpDir(no_path_tree);
  checkNoPathTree(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("no config history and no path tree");
  Metadata *no_conf_no_paths = genWithOnlyBackupPoints();
  writeMetadataToTmpDir(no_conf_no_paths);
  checkEmptyMetadata(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("empty metadata");
  Metadata *empty_metadata = createEmptyMetadata(0);
  checkEmptyMetadata(empty_metadata);
  writeMetadataToTmpDir(empty_metadata);
  checkEmptyMetadata(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("merging current backup into empty metadata");
  writeMetadataToTmpDir(initOnlyCurrentBackupData(createEmptyMetadata(0)));
  checkOnlyCurrentBackupData(metadataLoad(strWrap("tmp/metadata")));

  /* The same test as above, but with unreferenced backup points, which
     should be discarded while writing. */
  writeMetadataToTmpDir(initOnlyCurrentBackupData(genWithOnlyBackupPoints()));
  checkOnlyCurrentBackupData(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("wiping orphaned nodes");
  writeMetadataToTmpDir(genNodesToWipe());
  checkWipedNodes(metadataLoad(strWrap("tmp/metadata")));
  testGroupEnd();

  testGroupStart("reject corrupted metadata");
  testRejectingCorruptedMetadata();
  testGroupEnd();
}
