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

static void appendHistory(PathNode *node, size_t backup_id,
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

  history_point->backup = &metadata->backup_history[backup_id];
  metadata->backup_history[backup_id].ref_count =
    sSizeAdd(metadata->backup_history[backup_id].ref_count, 1);

  history_point->state = state;
  history_point->next = NULL;
}

static void appendHistNonExisting(PathNode *node, size_t backup_id,
                                  Metadata *metadata)
{
  PathState state = { .type = PST_non_existing };
  appendHistory(node, backup_id, metadata, state);
}

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
  appendHistory(node, backup_id, metadata, state);
}

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

  appendHistory(node, backup_id, metadata, state);
}

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

  appendHistory(node, backup_id, metadata, state);
}

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

  metadata->backup_history[0].id = 0;
  metadata->backup_history[0].timestamp = 1234;
  metadata->backup_history[0].ref_count = 0;

  metadata->backup_history[1].id = 1;
  metadata->backup_history[1].timestamp = 4321;
  metadata->backup_history[1].ref_count = 0;

  metadata->backup_history[2].id = 2;
  metadata->backup_history[2].timestamp = 7890;
  metadata->backup_history[2].ref_count = 0;

  metadata->backup_history[3].id = 3;
  metadata->backup_history[3].timestamp = 9876;
  metadata->backup_history[3].ref_count = 0;

  metadata->total_path_count = 0;
  metadata->path_table = strtableNew();

  PathNode *etc = createPathNode("etc", BPOL_none, NULL, metadata);
  metadata->paths = etc;

  appendHistDirectory(etc, 3, metadata, 12, 8,  2389478, 0777);
  appendHistDirectory(etc, 2, metadata, 7,  19, 12837,   0666);

  PathNode *conf = createPathNode("conf", BPOL_none, etc, metadata);
  PathNode *portage = createPathNode("portage", BPOL_track, etc, metadata);

  return metadata;
}

static void checkTestData1(Metadata *metadata)
{
  assert_true(metadata != NULL);
  assert_true(strCompare(metadata->repo_path, str("foo")));

  assert_true(metadata->current_backup.id == 0);
  assert_true(metadata->current_backup.timestamp == 0);
  assert_true(metadata->current_backup.ref_count == 0);

  assert_true(metadata->backup_history_length == 4);
  assert_true(metadata->backup_history != NULL);

  assert_true(metadata->backup_history[0].id == 0);
  assert_true(metadata->backup_history[0].timestamp == 1234);
  assert_true(metadata->backup_history[0].ref_count == 0);

  assert_true(metadata->backup_history[1].id == 1);
  assert_true(metadata->backup_history[1].timestamp == 4321);
  assert_true(metadata->backup_history[1].ref_count == 0);

  assert_true(metadata->backup_history[2].id == 2);
  assert_true(metadata->backup_history[2].timestamp == 7890);
  assert_true(metadata->backup_history[2].ref_count == 0);

  assert_true(metadata->backup_history[3].id == 3);
  assert_true(metadata->backup_history[3].timestamp == 9876);
  assert_true(metadata->backup_history[3].ref_count == 0);

  assert_true(metadata->total_path_count == 2);
  assert_true(metadata->paths != NULL);
}

int main(void)
{
  testGroupStart("generating simple test metadata");
  checkTestData1(genTestData1());
  testGroupEnd();
}
