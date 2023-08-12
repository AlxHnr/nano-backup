#include "backup.h"

#include "backup-common.h"
#include "backup-dummy-hashes.h"
#include "safe-wrappers.h"
#include "test-common.h"
#include "test.h"

/** Asserts that the given node contains a "dummy" subnode with the
  specified properties. The hash can be NULL. */
static void mustHaveDummy(PathNode *node, BackupHint hint, BackupPolicy policy, Backup *backup, const char *hash)
{
  PathNode *dummy = findSubnode(node, "dummy", hint, policy, 1, 0);
  mustHaveRegularStat(dummy, backup, 5, (const uint8_t *)hash, 0);
}

/** Creates various dummy files for testing change detection in nodes
  without a policy. */
static void initNoneChangeTest(SearchNode *change_detection_node)
{
  /* Generate various files. */
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/a/b");
  makeDir("tmp/files/a/c");
  makeDir("tmp/files/d");
  makeDir("tmp/files/d/e");
  makeDir("tmp/files/d/f");
  makeDir("tmp/files/g");
  makeDir("tmp/files/h");
  generateFile("tmp/files/a/b/dummy", "dummy", 1);
  generateFile("tmp/files/a/c/dummy", "dummy", 1);
  generateFile("tmp/files/d/e/dummy", "dummy", 1);
  generateFile("tmp/files/d/f/dummy", "dummy", 1);
  generateFile("tmp/files/g/dummy", "dummy", 1);
  generateFile("tmp/files/h/dummy", "dummy", 1);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 16);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth() + 16);

  PathNode *files = findFilesNode(metadata, BH_added, 4);

  PathNode *a = findSubnode(files, "a", BH_added, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_added, BPOL_copy, &metadata->current_backup, NULL);
  PathNode *c = findSubnode(a, "c", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_added, BPOL_track, &metadata->current_backup, NULL);

  PathNode *d = findSubnode(files, "d", BH_added, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_added, BPOL_mirror, &metadata->current_backup, NULL);
  PathNode *f = findSubnode(d, "f", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_added, BPOL_track, &metadata->current_backup, NULL);

  PathNode *g = findSubnode(files, "g", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_added, BPOL_track, &metadata->current_backup, NULL);

  PathNode *h = findSubnode(files, "h", BH_added, BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_added, BPOL_copy, &metadata->current_backup, NULL);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
  mustHaveDummy(b, BH_added, BPOL_copy, &metadata->current_backup, "dummy");
  mustHaveDummy(c, BH_added, BPOL_track, &metadata->current_backup, "dummy");
  mustHaveDummy(e, BH_added, BPOL_mirror, &metadata->current_backup, "dummy");
  mustHaveDummy(f, BH_added, BPOL_track, &metadata->current_backup, "dummy");
  mustHaveDummy(g, BH_added, BPOL_track, &metadata->current_backup, "dummy");
  mustHaveDummy(h, BH_added, BPOL_copy, &metadata->current_backup, "dummy");
}

/** Modifies the current metadata in such a way, that a subsequent
  initiation will find changes in nodes without a policy. */
static void modifyNoneChangeTest(SearchNode *change_detection_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps(0), cwd_depth() + 16);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 10);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth() + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps(0), 6);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 4);

  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_unchanged, BPOL_copy, &metadata->backup_history[0], "dummy");
  PathNode *c = findSubnode(a, "c", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_unchanged, BPOL_track, &metadata->backup_history[0], "dummy");

  PathNode *d = findSubnode(files, "d", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_unchanged, BPOL_mirror, &metadata->backup_history[0], "dummy");
  PathNode *f = findSubnode(d, "f", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_unchanged, BPOL_track, &metadata->backup_history[0], "dummy");

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_unchanged, BPOL_track, &metadata->backup_history[0], "dummy");

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_unchanged, BPOL_copy, &metadata->backup_history[0], "dummy");

  /* Modify various path nodes. */
  a->history->state.uid++;
  b->history->state.gid++;
  c->history->state.metadata.dir.mode++;
  d->history->state.metadata.dir.timestamp++;

  e->history->state.uid++;
  e->history->state.metadata.dir.mode++;

  f->history->state.gid++;
  f->history->state.metadata.dir.timestamp++;

  g->history->state.metadata.dir.mode++;
  g->history->state.metadata.dir.timestamp++;

  h->history->state.gid++;
  h->history->state.metadata.dir.mode++;
  h->history->state.metadata.dir.timestamp++;

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Tests detection of changes in nodes without a policy. */
static void changeNoneChangeTest(SearchNode *change_detection_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps(1), cwd_depth() + 10);
  checkHistPoint(metadata, 1, 1, phase_timestamps(0), 6);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 10);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps(1), 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps(0), 6);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 4);

  PathNode *a = findSubnode(files, "a", BH_owner_changed, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_owner_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");
  PathNode *c = findSubnode(a, "c", BH_permissions_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *d = findSubnode(files, "d", BH_timestamp_changed, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_owner_changed | BH_permissions_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_unchanged, BPOL_mirror, &metadata->backup_history[1], "dummy");
  PathNode *f = findSubnode(d, "f", BH_owner_changed | BH_timestamp_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *g = findSubnode(files, "g", BH_permissions_changed | BH_timestamp_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *h =
    findSubnode(files, "h", BH_owner_changed | BH_permissions_changed | BH_timestamp_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Tests metadata written by the previous phase. */
static void postNoneChangeTest(SearchNode *change_detection_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps(2), cwd_depth() + 10);
  checkHistPoint(metadata, 1, 1, phase_timestamps(0), 6);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 10);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 16);
  checkHistPoint(metadata, 0, 0, phase_timestamps(2), 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps(0), 6);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 4);

  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *b = findSubnode(a, "b", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(b, &metadata->current_backup);
  mustHaveDummy(b, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");
  PathNode *c = findSubnode(a, "c", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(c, &metadata->current_backup);
  mustHaveDummy(c, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *d = findSubnode(files, "d", BH_unchanged, BPOL_none, 1, 2);
  mustHaveDirectoryStat(d, &metadata->current_backup);
  PathNode *e = findSubnode(d, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  mustHaveDummy(e, BH_unchanged, BPOL_mirror, &metadata->backup_history[1], "dummy");
  PathNode *f = findSubnode(d, "f", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(f, &metadata->current_backup);
  mustHaveDummy(f, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(g, &metadata->current_backup);
  mustHaveDummy(g, BH_unchanged, BPOL_track, &metadata->backup_history[1], "dummy");

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(h, &metadata->current_backup);
  mustHaveDummy(h, BH_unchanged, BPOL_copy, &metadata->backup_history[1], "dummy");

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Prepares files and metadata for testing detection of changes in files.

  @param change_detection_node The search tree to use for preparing the
  test.
  @param policy The policy to test.
*/
static void initChangeDetectionTest(SearchNode *change_detection_node, BackupPolicy policy)
{
  /* Prepare test and create various files. */
  assertTmpIsCleared();
  makeDir("tmp/files/0");
  makeDir("tmp/files/0/1");
  makeDir("tmp/files/2");
  makeDir("tmp/files/3");
  makeDir("tmp/files/4");
  makeDir("tmp/files/5");
  makeDir("tmp/files/8");
  makeDir("tmp/files/13");
  makeDir("tmp/files/14");
  makeSymlink("/dev/non-existing", "tmp/files/5/6");
  makeSymlink("uid changing symlink", "tmp/files/15");
  makeSymlink("gid changing symlink", "tmp/files/16");
  makeSymlink("symlink content", "tmp/files/17");
  makeSymlink("symlink content", "tmp/files/18");
  makeSymlink("gid + content", "tmp/files/19");
  makeSymlink("content, uid, gid", "tmp/files/20");
  generateFile("tmp/files/5/7", "This is a test file\n", 20);
  generateFile("tmp/files/8/9", "This is a file\n", 1);
  generateFile("tmp/files/8/10", "GID and UID", 1);
  generateFile("tmp/files/8/11", "", 0);
  generateFile("tmp/files/8/12", "nano-backup ", 7);
  generateFile("tmp/files/21", "This is a super file\n", 100);
  generateFile("tmp/files/22", "Large\n", 200);
  generateFile("tmp/files/23", "nested-file ", 12);
  generateFile("tmp/files/24", "nested ", 8);
  generateFile("tmp/files/25", "a/b/c/", 7);
  generateFile("tmp/files/26", "Hello world\n", 2);
  generateFile("tmp/files/27", "m", 21);
  generateFile("tmp/files/28", "0", 2123);
  generateFile("tmp/files/29", "empty\n", 200);
  generateFile("tmp/files/30", "This is a test file\n", 20);
  generateFile("tmp/files/31", "This is a super file\n", 100);
  generateFile("tmp/files/32", "A small file", 1);
  generateFile("tmp/files/33", "Another file", 1);
  generateFile("tmp/files/34", "Some dummy text", 1);
  generateFile("tmp/files/35", "abcdefghijkl", 1);
  generateFile("tmp/files/36", "Nano Backup", 1);
  generateFile("tmp/files/37", "nested ", 8);
  generateFile("tmp/files/38", "", 0);
  generateFile("tmp/files/39", "", 0);
  generateFile("tmp/files/40", "", 0);
  generateFile("tmp/files/41", "random file", 1);
  generateFile("tmp/files/42", "", 0);
  generateFile("tmp/files/43", "Large\n", 200);
  generateFile("tmp/files/44", "nested-file ", 12);
  generateFile("tmp/files/45", "Small file", 1);
  generateFile("tmp/files/46", "Test file", 1);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 49);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth() + 49);

  PathNode *files = findFilesNode(metadata, BH_added, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_added, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->current_backup);
  PathNode *node_1 = findSubnode(node_0, "1", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->current_backup);
  PathNode *node_2 = findSubnode(files, "2", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->current_backup);
  PathNode *node_3 = findSubnode(files, "3", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  PathNode *node_4 = findSubnode(files, "4", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  PathNode *node_5 = findSubnode(files, "5", BH_added, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->current_backup);
  PathNode *node_6 = findSubnode(node_5, "6", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "/dev/non-existing");
  PathNode *node_7 = findSubnode(node_5, "7", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, NULL, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_added, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->current_backup);
  PathNode *node_9 = findSubnode(node_8, "9", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 15, NULL, 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_12 = findSubnode(node_8, "12", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 84, NULL, 0);
  PathNode *node_13 = findSubnode(files, "13", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->current_backup);
  PathNode *node_14 = findSubnode(files, "14", BH_added, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->current_backup);
  PathNode *node_15 = findSubnode(files, "15", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->current_backup, "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->current_backup, "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->current_backup, "symlink content");
  PathNode *node_18 = findSubnode(files, "18", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->current_backup, "symlink content");
  PathNode *node_19 = findSubnode(files, "19", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->current_backup, "gid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_added, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->current_backup, "content, uid, gid");
  PathNode *node_21 = findSubnode(files, "21", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, NULL, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, NULL, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 56, NULL, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->current_backup, 42, NULL, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 24, NULL, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, NULL, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2123, NULL, 0);
  PathNode *node_29 = findSubnode(files, "29", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_30 = findSubnode(files, "30", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, NULL, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, NULL, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_32, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_33 = findSubnode(files, "33", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_34 = findSubnode(files, "34", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, NULL, 0);
  PathNode *node_35 = findSubnode(files, "35", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 12, NULL, 0);
  PathNode *node_36 = findSubnode(files, "36", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_37 = findSubnode(files, "37", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 56, NULL, 0);
  PathNode *node_38 = findSubnode(files, "38", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_38, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_39 = findSubnode(files, "39", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_40 = findSubnode(files, "40", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_41 = findSubnode(files, "41", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 11, NULL, 0);
  PathNode *node_42 = findSubnode(files, "42", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_42, &metadata->current_backup, 0, NULL, 0);
  PathNode *node_43 = findSubnode(files, "43", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 1200, NULL, 0);
  PathNode *node_44 = findSubnode(files, "44", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 144, NULL, 0);
  PathNode *node_45 = findSubnode(files, "45", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_45, &metadata->current_backup, 10, NULL, 0);
  PathNode *node_46 = findSubnode(files, "46", BH_added, policy, 1, 0);
  mustHaveRegularStat(node_46, &metadata->current_backup, 9, NULL, 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 31);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 15, (uint8_t *)"This is a file\n", 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, (uint8_t *)"GID and UID", 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 84, some_file_hash, 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, nested_1_hash, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 56, nested_2_hash, 0);
  mustHaveRegularStat(node_25, &metadata->current_backup, 42, test_c_hash, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 24, nb_a_abc_1_hash, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, nb_manual_b_hash, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2123, bin_hash, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, bin_c_1_hash, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(node_32, &metadata->current_backup, 12, (uint8_t *)"A small file", 0);
  mustHaveRegularStat(node_33, &metadata->current_backup, 12, (uint8_t *)"Another file", 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, (uint8_t *)"Some dummy text", 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 12, (uint8_t *)"abcdefghijkl", 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, (uint8_t *)"Nano Backup", 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 56, nested_2_hash, 0);
  mustHaveRegularStat(node_38, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 11, (uint8_t *)"random file", 0);
  mustHaveRegularStat(node_42, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 1200, data_d_hash, 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 144, nested_1_hash, 0);
  mustHaveRegularStat(node_45, &metadata->current_backup, 10, (uint8_t *)"Small file", 0);
  mustHaveRegularStat(node_46, &metadata->current_backup, 9, (uint8_t *)"Test file", 0);
}

/** Modifies the current metadata in such a way, that a subsequent
  initiation will find changes in nodes. */
static void modifyChangeDetectionTest(SearchNode *change_detection_node, BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 49);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 2);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 47);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->backup_history[0]);
  PathNode *node_1 = findSubnode(node_0, "1", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->backup_history[0]);
  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->backup_history[0]);
  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0]);
  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0]);
  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->backup_history[0]);
  PathNode *node_6 = findSubnode(node_5, "6", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0], "/dev/non-existing");
  PathNode *node_7 = findSubnode(node_5, "7", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->backup_history[0]);
  PathNode *node_9 = findSubnode(node_8, "9", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->backup_history[0], 15, (uint8_t *)"This is a file\n", 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->backup_history[0], 11, (uint8_t *)"GID and UID", 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_12 = findSubnode(node_8, "12", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->backup_history[0], 84, some_file_hash, 0);
  PathNode *node_13 = findSubnode(files, "13", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->backup_history[0]);
  PathNode *node_14 = findSubnode(files, "14", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->backup_history[0]);
  PathNode *node_15 = findSubnode(files, "15", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->backup_history[0], "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->backup_history[0], "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[0], "symlink content");
  PathNode *node_18 = findSubnode(files, "18", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[0], "symlink content");
  PathNode *node_19 = findSubnode(files, "19", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->backup_history[0], "gid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->backup_history[0], "content, uid, gid");
  PathNode *node_21 = findSubnode(files, "21", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[0], 56, nested_2_hash, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[0], 42, test_c_hash, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->backup_history[0], 24, nb_a_abc_1_hash, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->backup_history[0], 2123, bin_hash, 0);
  PathNode *node_29 = findSubnode(files, "29", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->backup_history[0], 1200, bin_c_1_hash, 0);
  PathNode *node_30 = findSubnode(files, "30", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[0], 12, (uint8_t *)"A small file", 0);
  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[0], 12, (uint8_t *)"Another file", 0);
  PathNode *node_34 = findSubnode(files, "34", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->backup_history[0], 15, (uint8_t *)"Some dummy text", 0);
  PathNode *node_35 = findSubnode(files, "35", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->backup_history[0], 12, (uint8_t *)"abcdefghijkl", 0);
  PathNode *node_36 = findSubnode(files, "36", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->backup_history[0], 11, (uint8_t *)"Nano Backup", 0);
  PathNode *node_37 = findSubnode(files, "37", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[0], 56, nested_2_hash, 0);
  PathNode *node_38 = findSubnode(files, "38", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_39 = findSubnode(files, "39", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_40 = findSubnode(files, "40", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_41 = findSubnode(files, "41", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->backup_history[0], 11, (uint8_t *)"random file", 0);
  PathNode *node_42 = findSubnode(files, "42", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_42, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_43 = findSubnode(files, "43", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *node_44 = findSubnode(files, "44", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *node_45 = findSubnode(files, "45", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[0], 10, (uint8_t *)"Small file", 0);
  PathNode *node_46 = findSubnode(files, "46", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_46, &metadata->backup_history[0], 9, (uint8_t *)"Test file", 0);

  /* Modify various path nodes. */
  node_0->history->state.uid++;
  node_1->history->state.gid++;
  node_2->history->state.metadata.dir.mode++;
  node_3->history->state.metadata.dir.timestamp++;
  node_4->history->state.metadata.dir.mode++;
  node_4->history->state.metadata.dir.timestamp++;
  node_5->history->state.uid++;
  node_5->history->state.metadata.dir.mode++;

  remakeSymlink("/dev/null", "tmp/files/5/6");
  node_6->history->state.uid++;

  node_7->history->state.uid++;
  node_8->history->state.gid++;
  node_8->history->state.metadata.dir.timestamp++;

  regenerateFile(node_9, "This is test", 1);
  node_9->history->state.uid++;

  node_10->history->state.metadata.reg.timestamp++;
  node_11->history->state.uid++;
  node_11->history->state.metadata.reg.mode++;

  regenerateFile(node_12, "a short string", 1);
  node_12->history->state.gid++;
  node_12->history->state.metadata.reg.mode++;

  node_13->history->state.gid++;
  node_13->history->state.metadata.dir.mode++;
  node_13->history->state.metadata.dir.timestamp++;
  node_14->history->state.uid++;
  node_14->history->state.metadata.dir.timestamp++;
  node_15->history->state.uid++;
  node_16->history->state.gid++;
  remakeSymlink("symlink-content", "tmp/files/17");
  remakeSymlink("symlink content string", "tmp/files/18");

  remakeSymlink("uid + content", "tmp/files/19");
  node_19->history->state.gid++;

  remakeSymlink("content, uid, gid ", "tmp/files/20");
  node_20->history->state.uid++;
  node_20->history->state.gid++;

  node_21->history->state.gid++;
  node_22->history->state.metadata.reg.mode++;
  node_23->history->state.metadata.reg.timestamp++;
  regenerateFile(node_24, "nested ", 9);
  regenerateFile(node_25, "a/B/c/", 7);

  regenerateFile(node_26, "Hello world", 2);
  node_26->history->state.gid++;

  regenerateFile(node_27, "M", 21);
  node_27->history->state.metadata.reg.mode++;

  regenerateFile(node_28, "0", 2124);
  node_28->history->state.metadata.reg.timestamp++;

  regenerateFile(node_29, "Empty\n", 200);
  node_29->history->state.uid++;
  node_29->history->state.metadata.reg.timestamp++;

  node_30->history->state.uid++;
  node_30->history->state.metadata.reg.mode++;
  node_30->history->state.metadata.reg.timestamp++;
  node_31->history->state.uid++;
  node_31->history->state.gid++;
  regenerateFile(node_32, "A small file.", 1);
  regenerateFile(node_33, "another file", 1);

  regenerateFile(node_34, "some dummy text", 1);
  node_34->history->state.metadata.reg.timestamp++;

  regenerateFile(node_35, "?", 1);
  node_35->history->state.metadata.reg.mode++;

  regenerateFile(node_36, "nano backup", 1);
  node_36->history->state.gid++;
  node_36->history->state.metadata.reg.mode++;

  regenerateFile(node_37, "", 0);
  regenerateFile(node_38, "@", 1);
  node_39->history->state.gid++;
  node_40->history->state.metadata.reg.timestamp++;

  regenerateFile(node_41, "", 0);
  node_41->history->state.metadata.reg.mode++;

  regenerateFile(node_42, "Backup\n", 74);
  node_42->history->state.gid++;

  regenerateFile(node_43, "Large\n", 2);
  node_43->history->state.metadata.reg.timestamp++;

  regenerateFile(node_44, "Q", 20);
  regenerateFile(node_45, "q", 21);

  regenerateFile(node_46, "test\n", 123);
  node_46->history->state.uid++;

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 31);
}

/** Tests the changes injected by modifyChangeDetectionTest(). */
static void changeDetectionTest(SearchNode *change_detection_node, BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 47);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 47);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 2);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_owner_changed, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->current_backup);
  PathNode *node_1 = findSubnode(node_0, "1", BH_owner_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->current_backup);
  PathNode *node_2 = findSubnode(files, "2", BH_permissions_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->current_backup);
  PathNode *node_3 = findSubnode(files, "3", BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  PathNode *node_4 = findSubnode(files, "4", BH_permissions_changed | BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  PathNode *node_5 = findSubnode(files, "5", BH_owner_changed | BH_permissions_changed, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->current_backup);
  PathNode *node_6 = findSubnode(node_5, "6", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "/dev/null");
  PathNode *node_7 = findSubnode(node_5, "7", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, three_hash, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_owner_changed | BH_timestamp_changed, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->current_backup);
  PathNode *node_9 = findSubnode(node_8, "9", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 12, (uint8_t *)"This is a file\n", 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, (uint8_t *)"GID and UID", 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_owner_changed | BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, (uint8_t *)"", 0);
  PathNode *node_12 =
    findSubnode(node_8, "12", BH_owner_changed | BH_permissions_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 14, some_file_hash, 0);
  PathNode *node_13 =
    findSubnode(files, "13", BH_owner_changed | BH_permissions_changed | BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->current_backup);
  PathNode *node_14 = findSubnode(files, "14", BH_owner_changed | BH_timestamp_changed, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->current_backup);
  PathNode *node_15 = findSubnode(files, "15", BH_owner_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->current_backup, "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_owner_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->current_backup, "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->current_backup, "symlink-content");
  PathNode *node_18 = findSubnode(files, "18", BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->current_backup, "symlink content string");
  PathNode *node_19 = findSubnode(files, "19", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->current_backup, "uid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->current_backup, "content, uid, gid ");
  PathNode *node_21 = findSubnode(files, "21", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, nested_1_hash, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 63, nested_2_hash, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_owner_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 22, nb_a_abc_1_hash, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, nb_manual_b_hash, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_timestamp_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2124, bin_hash, 0);
  PathNode *node_29 = findSubnode(
    files, "29", BH_owner_changed | BH_timestamp_changed | BH_content_changed | BH_fresh_hash, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, node_29_hash, 0);
  PathNode *node_30 =
    findSubnode(files, "30", BH_owner_changed | BH_permissions_changed | BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, three_hash, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_content_changed, policy, 1, 0);
  node_32->history->state.metadata.reg.hash[12] = '?';
  mustHaveRegularStat(node_32, &metadata->current_backup, 13, (uint8_t *)"A small file??", 0);
  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);
  PathNode *node_34 =
    findSubnode(files, "34", BH_timestamp_changed | BH_content_changed | BH_fresh_hash, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, (uint8_t *)"some dummy text", 0);
  PathNode *node_35 = findSubnode(files, "35", BH_permissions_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 1, (uint8_t *)"abcdefghijkl", 0);
  PathNode *node_36 = findSubnode(files, "36", BH_owner_changed | BH_permissions_changed, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, (uint8_t *)"Nano Backup", 0);
  PathNode *node_37 = findSubnode(files, "37", BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 0, nested_2_hash, 0);
  PathNode *node_38 = findSubnode(files, "38", BH_content_changed, policy, 1, 0);
  node_38->history->state.metadata.reg.hash[0] = 'P';
  mustHaveRegularStat(node_38, &metadata->current_backup, 1, (uint8_t *)"PPP", 0);
  PathNode *node_39 = findSubnode(files, "39", BH_owner_changed, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, (uint8_t *)"", 0);
  PathNode *node_40 = findSubnode(files, "40", BH_timestamp_changed, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, (uint8_t *)"", 0);
  PathNode *node_41 = findSubnode(files, "41", BH_permissions_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 0, (uint8_t *)"random file", 0);
  PathNode *node_42 = findSubnode(files, "42", BH_owner_changed | BH_content_changed, policy, 1, 0);
  memset(node_42->history->state.metadata.reg.hash, 'X', FILE_HASH_SIZE);
  node_42->history->state.metadata.reg.slot = 52;
  mustHaveRegularStat(node_42, &metadata->current_backup, 518, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 52);
  PathNode *node_43 = findSubnode(files, "43", BH_timestamp_changed | BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 12, data_d_hash, 0);
  PathNode *node_44 = findSubnode(files, "44", BH_content_changed, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 20, nested_1_hash, 0);
  PathNode *node_45 = findSubnode(files, "45", BH_content_changed, policy, 1, 0);
  memset(&node_45->history->state.metadata.reg.hash[10], 'J', 10);
  node_45->history->state.metadata.reg.slot = 149;
  mustHaveRegularStat(node_45, &metadata->current_backup, 21, (uint8_t *)"Small fileJJJJJJJJJJ", 149);
  PathNode *node_46 = findSubnode(files, "46", BH_owner_changed | BH_content_changed, policy, 1, 0);
  memset(&node_46->history->state.metadata.reg.hash[9], '=', 11);
  node_46->history->state.metadata.reg.slot = 2;
  mustHaveRegularStat(node_46, &metadata->current_backup, 615, (uint8_t *)"Test file===========", 2);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 47);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 12, (uint8_t *)"This is test", 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, (uint8_t *)"GID and UID", 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 14, (uint8_t *)"a short string", 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, nested_1_hash, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 63, node_24_hash, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 22, node_26_hash, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, nb_manual_b_hash, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2124, node_28_hash, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, node_29_hash, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(node_32, &metadata->current_backup, 13, (uint8_t *)"A small file.", 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, (uint8_t *)"some dummy text", 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 1, (uint8_t *)"?", 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, (uint8_t *)"Nano Backup", 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_38, &metadata->current_backup, 1, (uint8_t *)"@", 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_42, &metadata->current_backup, 518, node_42_hash, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 12, (uint8_t *)"Large\nLarge\n", 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 20, (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  mustHaveRegularStat(node_45, &metadata->current_backup, 21, node_45_hash, 0);
  mustHaveRegularStat(node_46, &metadata->current_backup, 615, node_46_hash, 0);
}

/** Tests the metadata written by changeDetectionTest() and cleans up the
  test directory. */
static void postDetectionTest(SearchNode *change_detection_node, BackupPolicy policy)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 47);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 3), 2);
  initiateBackup(metadata, change_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 2);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 45);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 3), 2);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_unchanged, policy, 1, 1);
  mustHaveDirectoryStat(node_0, &metadata->backup_history[0]);
  PathNode *node_1 = findSubnode(node_0, "1", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_1, &metadata->backup_history[0]);
  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_2, &metadata->backup_history[0]);
  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0]);
  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0]);
  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, policy, 1, 2);
  mustHaveDirectoryStat(node_5, &metadata->backup_history[0]);
  PathNode *node_6 = findSubnode(node_5, "6", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0], "/dev/null");
  PathNode *node_7 = findSubnode(node_5, "7", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_7, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, policy, 1, 4);
  mustHaveDirectoryStat(node_8, &metadata->backup_history[0]);
  PathNode *node_9 = findSubnode(node_8, "9", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_9, &metadata->backup_history[0], 12, (uint8_t *)"This is test", 0);
  PathNode *node_10 = findSubnode(node_8, "10", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_10, &metadata->backup_history[0], 11, (uint8_t *)"GID and UID", 0);
  PathNode *node_11 = findSubnode(node_8, "11", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_11, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_12 = findSubnode(node_8, "12", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_12, &metadata->backup_history[0], 14, (uint8_t *)"a short string", 0);
  PathNode *node_13 = findSubnode(files, "13", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_13, &metadata->backup_history[0]);
  PathNode *node_14 = findSubnode(files, "14", BH_unchanged, policy, 1, 0);
  mustHaveDirectoryStat(node_14, &metadata->backup_history[0]);
  PathNode *node_15 = findSubnode(files, "15", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_15, &metadata->backup_history[0], "uid changing symlink");
  PathNode *node_16 = findSubnode(files, "16", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_16, &metadata->backup_history[0], "gid changing symlink");
  PathNode *node_17 = findSubnode(files, "17", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[0], "symlink-content");
  PathNode *node_18 = findSubnode(files, "18", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[0], "symlink content string");
  PathNode *node_19 = findSubnode(files, "19", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_19, &metadata->backup_history[0], "uid + content");
  PathNode *node_20 = findSubnode(files, "20", BH_unchanged, policy, 1, 0);
  mustHaveSymlinkLStat(node_20, &metadata->backup_history[0], "content, uid, gid ");
  PathNode *node_21 = findSubnode(files, "21", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_21, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_22 = findSubnode(files, "22", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_22, &metadata->backup_history[0], 1200, data_d_hash, 0);
  PathNode *node_23 = findSubnode(files, "23", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_23, &metadata->backup_history[0], 144, nested_1_hash, 0);
  PathNode *node_24 = findSubnode(files, "24", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[0], 63, node_24_hash, 0);
  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);
  PathNode *node_26 = findSubnode(files, "26", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_26, &metadata->backup_history[0], 22, node_26_hash, 0);
  PathNode *node_27 = findSubnode(files, "27", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_27, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  PathNode *node_28 = findSubnode(files, "28", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_28, &metadata->backup_history[0], 2124, node_28_hash, 0);
  PathNode *node_29 = findSubnode(files, "29", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_29, &metadata->backup_history[0], 1200, node_29_hash, 0);
  PathNode *node_30 = findSubnode(files, "30", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_30, &metadata->backup_history[0], 400, three_hash, 0);
  PathNode *node_31 = findSubnode(files, "31", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_31, &metadata->backup_history[0], 2100, super_hash, 0);
  PathNode *node_32 = findSubnode(files, "32", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[0], 13, (uint8_t *)"A small file.", 0);
  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);
  PathNode *node_34 = findSubnode(files, "34", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_34, &metadata->backup_history[0], 15, (uint8_t *)"some dummy text", 0);
  PathNode *node_35 = findSubnode(files, "35", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_35, &metadata->backup_history[0], 1, (uint8_t *)"?", 0);
  PathNode *node_36 = findSubnode(files, "36", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_36, &metadata->backup_history[0], 11, (uint8_t *)"Nano Backup", 0);
  PathNode *node_37 = findSubnode(files, "37", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_38 = findSubnode(files, "38", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[0], 1, (uint8_t *)"@", 0);
  PathNode *node_39 = findSubnode(files, "39", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_39, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_40 = findSubnode(files, "40", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_40, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_41 = findSubnode(files, "41", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_41, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  PathNode *node_42 = findSubnode(files, "42", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_42, &metadata->backup_history[0], 518, node_42_hash, 0);
  PathNode *node_43 = findSubnode(files, "43", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_43, &metadata->backup_history[0], 12, (uint8_t *)"Large\nLarge\n", 0);
  PathNode *node_44 = findSubnode(files, "44", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[0], 20, (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  PathNode *node_45 = findSubnode(files, "45", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[0], 21, node_45_hash, 0);
  PathNode *node_46 = findSubnode(files, "46", BH_unchanged, policy, 1, 0);
  mustHaveRegularStat(node_46, &metadata->backup_history[0], 615, node_46_hash, 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 47);
}

/** Tests change detection in tracked nodes. */
static void trackChangeDetectionTest(SearchNode *track_detection_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(13), cwd_depth() + 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps(12), 47);
  initiateBackup(metadata, track_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 47);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(13), 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps(12), 47);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_owner_changed, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_0, &metadata->current_backup);
  struct stat node_0_stats = sStat(node_0->path);
  node_0_stats.st_uid++;
  mustHaveDirectoryStats(node_0, &metadata->backup_history[1], node_0_stats);

  PathNode *node_1 = findSubnode(node_0, "1", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_1, &metadata->current_backup);
  struct stat node_1_stats = sStat(node_1->path);
  node_1_stats.st_gid++;
  mustHaveDirectoryStats(node_1, &metadata->backup_history[1], node_1_stats);

  PathNode *node_2 = findSubnode(files, "2", BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_2, &metadata->current_backup);
  struct stat node_2_stats = sStat(node_2->path);
  node_2_stats.st_mode++;
  mustHaveDirectoryStats(node_2, &metadata->backup_history[1], node_2_stats);

  PathNode *node_3 = findSubnode(files, "3", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_3, &metadata->current_backup);
  struct stat node_3_stats = sStat(node_3->path);
  node_3_stats.st_mtime++;
  mustHaveDirectoryStats(node_3, &metadata->backup_history[1], node_3_stats);

  PathNode *node_4 = findSubnode(files, "4", BH_permissions_changed | BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_4, &metadata->current_backup);
  struct stat node_4_stats = sStat(node_4->path);
  node_4_stats.st_mode++;
  node_4_stats.st_mtime++;
  mustHaveDirectoryStats(node_4, &metadata->backup_history[1], node_4_stats);

  PathNode *node_5 = findSubnode(files, "5", BH_owner_changed | BH_permissions_changed, BPOL_track, 2, 2);
  mustHaveDirectoryStat(node_5, &metadata->current_backup);
  struct stat node_5_stats = sStat(node_5->path);
  node_5_stats.st_uid++;
  node_5_stats.st_mode++;
  mustHaveDirectoryStats(node_5, &metadata->backup_history[1], node_5_stats);

  PathNode *node_6 = findSubnode(node_5, "6", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_6, &metadata->current_backup, "/dev/null");
  struct stat node_6_stats = sLStat(node_6->path);
  node_6_stats.st_uid++;
  mustHaveSymlinkStats(node_6, &metadata->backup_history[1], node_6_stats, "/dev/non-existing");

  PathNode *node_7 = findSubnode(node_5, "7", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, three_hash, 0);
  struct stat node_7_stats = sStat(node_7->path);
  node_7_stats.st_uid++;
  mustHaveRegularStats(node_7, &metadata->backup_history[1], node_7_stats, 400, three_hash, 0);

  PathNode *node_8 = findSubnode(files, "8", BH_owner_changed | BH_timestamp_changed, BPOL_track, 2, 4);
  mustHaveDirectoryStat(node_8, &metadata->current_backup);
  struct stat node_8_stats = sStat(node_8->path);
  node_8_stats.st_gid++;
  node_8_stats.st_mtime++;
  mustHaveDirectoryStats(node_8, &metadata->backup_history[1], node_8_stats);

  PathNode *node_9 = findSubnode(node_8, "9", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 12, (uint8_t *)"This is a file\n", 0);
  struct stat node_9_stats = sStat(node_9->path);
  node_9_stats.st_uid++;
  mustHaveRegularStats(node_9, &metadata->backup_history[1], node_9_stats, 15, (uint8_t *)"This is a file\n", 0);

  PathNode *node_10 = findSubnode(node_8, "10", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, (uint8_t *)"GID and UID", 0);
  struct stat node_10_stats = sStat(node_10->path);
  node_10_stats.st_mtime++;
  mustHaveRegularStats(node_10, &metadata->backup_history[1], node_10_stats, 11, (uint8_t *)"GID and UID", 0);

  PathNode *node_11 = findSubnode(node_8, "11", BH_owner_changed | BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, (uint8_t *)"", 0);
  struct stat node_11_stats = sStat(node_11->path);
  node_11_stats.st_uid++;
  node_11_stats.st_mode++;
  mustHaveRegularStats(node_11, &metadata->backup_history[1], node_11_stats, 0, (uint8_t *)"", 0);

  PathNode *node_12 =
    findSubnode(node_8, "12", BH_owner_changed | BH_permissions_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 14, some_file_hash, 0);
  struct stat node_12_stats = sStat(node_12->path);
  node_12_stats.st_gid++;
  node_12_stats.st_mode++;
  mustHaveRegularStats(node_12, &metadata->backup_history[1], node_12_stats, 84, some_file_hash, 0);

  PathNode *node_13 =
    findSubnode(files, "13", BH_owner_changed | BH_permissions_changed | BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_13, &metadata->current_backup);
  struct stat node_13_stats = sStat(node_13->path);
  node_13_stats.st_gid++;
  node_13_stats.st_mode++;
  node_13_stats.st_mtime++;
  mustHaveDirectoryStats(node_13, &metadata->backup_history[1], node_13_stats);

  PathNode *node_14 = findSubnode(files, "14", BH_owner_changed | BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_14, &metadata->current_backup);
  struct stat node_14_stats = sStat(node_14->path);
  node_14_stats.st_uid++;
  node_14_stats.st_mtime++;
  mustHaveDirectoryStats(node_14, &metadata->backup_history[1], node_14_stats);

  PathNode *node_15 = findSubnode(files, "15", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_15, &metadata->current_backup, "uid changing symlink");
  struct stat node_15_stats = sLStat(node_15->path);
  node_15_stats.st_uid++;
  mustHaveSymlinkStats(node_15, &metadata->backup_history[1], node_15_stats, "uid changing symlink");

  PathNode *node_16 = findSubnode(files, "16", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_16, &metadata->current_backup, "gid changing symlink");
  struct stat node_16_stats = sLStat(node_16->path);
  node_16_stats.st_gid++;
  mustHaveSymlinkStats(node_16, &metadata->backup_history[1], node_16_stats, "gid changing symlink");

  PathNode *node_17 = findSubnode(files, "17", BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_17, &metadata->current_backup, "symlink-content");
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[1], "symlink content");

  PathNode *node_18 = findSubnode(files, "18", BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_18, &metadata->current_backup, "symlink content string");
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[1], "symlink content");

  PathNode *node_19 = findSubnode(files, "19", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_19, &metadata->current_backup, "uid + content");
  struct stat node_19_stats = sLStat(node_19->path);
  node_19_stats.st_gid++;
  mustHaveSymlinkStats(node_19, &metadata->backup_history[1], node_19_stats, "gid + content");

  PathNode *node_20 = findSubnode(files, "20", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_20, &metadata->current_backup, "content, uid, gid ");
  struct stat node_20_stats = sLStat(node_20->path);
  node_20_stats.st_uid++;
  node_20_stats.st_gid++;
  mustHaveSymlinkStats(node_20, &metadata->backup_history[1], node_20_stats, "content, uid, gid");

  PathNode *node_21 = findSubnode(files, "21", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash, 0);
  struct stat node_21_stats = sStat(node_21->path);
  node_21_stats.st_gid++;
  mustHaveRegularStats(node_21, &metadata->backup_history[1], node_21_stats, 2100, super_hash, 0);

  PathNode *node_22 = findSubnode(files, "22", BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash, 0);
  struct stat node_22_stats = sStat(node_22->path);
  node_22_stats.st_mode++;
  mustHaveRegularStats(node_22, &metadata->backup_history[1], node_22_stats, 1200, data_d_hash, 0);

  PathNode *node_23 = findSubnode(files, "23", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, nested_1_hash, 0);
  struct stat node_23_stats = sStat(node_23->path);
  node_23_stats.st_mtime++;
  mustHaveRegularStats(node_23, &metadata->backup_history[1], node_23_stats, 144, nested_1_hash, 0);

  PathNode *node_24 = findSubnode(files, "24", BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 63, nested_2_hash, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[1], 56, nested_2_hash, 0);

  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);

  PathNode *node_26 = findSubnode(files, "26", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 22, nb_a_abc_1_hash, 0);
  struct stat node_26_stats = sStat(node_26->path);
  node_26_stats.st_gid++;
  mustHaveRegularStats(node_26, &metadata->backup_history[1], node_26_stats, 24, nb_a_abc_1_hash, 0);

  PathNode *node_27 = findSubnode(files, "27", BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, nb_manual_b_hash, 0);
  struct stat node_27_stats = sStat(node_27->path);
  node_27_stats.st_mode++;
  mustHaveRegularStats(node_27, &metadata->backup_history[1], node_27_stats, 21, nb_manual_b_hash, 0);

  PathNode *node_28 = findSubnode(files, "28", BH_timestamp_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2124, bin_hash, 0);
  struct stat node_28_stats = sStat(node_28->path);
  node_28_stats.st_mtime++;
  mustHaveRegularStats(node_28, &metadata->backup_history[1], node_28_stats, 2123, bin_hash, 0);

  PathNode *node_29 = findSubnode(
    files, "29", BH_owner_changed | BH_timestamp_changed | BH_content_changed | BH_fresh_hash, BPOL_track, 2, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, node_29_hash, 0);
  struct stat node_29_stats = sStat(node_29->path);
  node_29_stats.st_uid++;
  node_29_stats.st_mtime++;
  mustHaveRegularStats(node_29, &metadata->backup_history[1], node_29_stats, 1200, bin_c_1_hash, 0);

  PathNode *node_30 =
    findSubnode(files, "30", BH_owner_changed | BH_permissions_changed | BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, three_hash, 0);
  struct stat node_30_stats = sStat(node_30->path);
  node_30_stats.st_uid++;
  node_30_stats.st_mode++;
  node_30_stats.st_mtime++;
  mustHaveRegularStats(node_30, &metadata->backup_history[1], node_30_stats, 400, three_hash, 0);

  PathNode *node_31 = findSubnode(files, "31", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash, 0);
  struct stat node_31_stats = sStat(node_31->path);
  node_31_stats.st_uid++;
  node_31_stats.st_gid++;
  mustHaveRegularStats(node_31, &metadata->backup_history[1], node_31_stats, 2100, super_hash, 0);

  PathNode *node_32 = findSubnode(files, "32", BH_content_changed, BPOL_track, 2, 0);
  node_32->history->state.metadata.reg.hash[12] = '?';
  mustHaveRegularStat(node_32, &metadata->current_backup, 13, (uint8_t *)"A small file??", 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);

  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);

  PathNode *node_34 =
    findSubnode(files, "34", BH_timestamp_changed | BH_content_changed | BH_fresh_hash, BPOL_track, 2, 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, (uint8_t *)"some dummy text", 0);
  struct stat node_34_stats = sStat(node_34->path);
  node_34_stats.st_mtime++;
  mustHaveRegularStats(node_34, &metadata->backup_history[1], node_34_stats, 15, (uint8_t *)"Some dummy text", 0);

  PathNode *node_35 = findSubnode(files, "35", BH_permissions_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 1, (uint8_t *)"abcdefghijkl", 0);
  struct stat node_35_stats = sStat(node_35->path);
  node_35_stats.st_mode++;
  mustHaveRegularStats(node_35, &metadata->backup_history[1], node_35_stats, 12, (uint8_t *)"abcdefghijkl", 0);

  PathNode *node_36 = findSubnode(files, "36", BH_owner_changed | BH_permissions_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, (uint8_t *)"Nano Backup", 0);
  struct stat node_36_stats = sStat(node_36->path);
  node_36_stats.st_gid++;
  node_36_stats.st_mode++;
  mustHaveRegularStats(node_36, &metadata->backup_history[1], node_36_stats, 11, (uint8_t *)"Nano Backup", 0);

  PathNode *node_37 = findSubnode(files, "37", BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 0, nested_2_hash, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[1], 56, nested_2_hash, 0);

  PathNode *node_38 = findSubnode(files, "38", BH_content_changed, BPOL_track, 2, 0);
  node_38->history->state.metadata.reg.hash[0] = 'P';
  mustHaveRegularStat(node_38, &metadata->current_backup, 1, (uint8_t *)"PPP", 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *node_39 = findSubnode(files, "39", BH_owner_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, (uint8_t *)"", 0);
  struct stat node_39_stats = sStat(node_39->path);
  node_39_stats.st_gid++;
  mustHaveRegularStats(node_39, &metadata->backup_history[1], node_39_stats, 0, (uint8_t *)"", 0);

  PathNode *node_40 = findSubnode(files, "40", BH_timestamp_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, (uint8_t *)"", 0);
  struct stat node_40_stats = sStat(node_40->path);
  node_40_stats.st_mtime++;
  mustHaveRegularStats(node_40, &metadata->backup_history[1], node_40_stats, 0, (uint8_t *)"", 0);

  PathNode *node_41 = findSubnode(files, "41", BH_permissions_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 0, (uint8_t *)"random file", 0);
  struct stat node_41_stats = sStat(node_41->path);
  node_41_stats.st_mode++;
  mustHaveRegularStats(node_41, &metadata->backup_history[1], node_41_stats, 11, (uint8_t *)"random file", 0);

  PathNode *node_42 = findSubnode(files, "42", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  memset(node_42->history->state.metadata.reg.hash, 'X', FILE_HASH_SIZE);
  node_42->history->state.metadata.reg.slot = 7;
  mustHaveRegularStat(node_42, &metadata->current_backup, 518, (uint8_t *)"XXXXXXXXXXXXXXXXXXXX", 7);
  struct stat node_42_stats = sStat(node_42->path);
  node_42_stats.st_gid++;
  mustHaveRegularStats(node_42, &metadata->backup_history[1], node_42_stats, 0, (uint8_t *)"", 0);

  PathNode *node_43 = findSubnode(files, "43", BH_timestamp_changed | BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 12, data_d_hash, 0);
  struct stat node_43_stats = sStat(node_43->path);
  node_43_stats.st_mtime++;
  mustHaveRegularStats(node_43, &metadata->backup_history[1], node_43_stats, 1200, data_d_hash, 0);

  PathNode *node_44 = findSubnode(files, "44", BH_content_changed, BPOL_track, 2, 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 20, nested_1_hash, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[1], 144, nested_1_hash, 0);

  PathNode *node_45 = findSubnode(files, "45", BH_content_changed, BPOL_track, 2, 0);
  memset(&node_45->history->state.metadata.reg.hash[10], 'J', 10);
  node_45->history->state.metadata.reg.slot = 99;
  mustHaveRegularStat(node_45, &metadata->current_backup, 21, (uint8_t *)"Small fileJJJJJJJJJJ", 99);
  mustHaveRegularStat(node_45, &metadata->backup_history[1], 10, (uint8_t *)"Small file", 0);

  PathNode *node_46 = findSubnode(files, "46", BH_owner_changed | BH_content_changed, BPOL_track, 2, 0);
  memset(&node_46->history->state.metadata.reg.hash[9], '=', 11);
  node_46->history->state.metadata.reg.slot = 0;
  mustHaveRegularStat(node_46, &metadata->current_backup, 615, (uint8_t *)"Test file===========", 0);
  struct stat node_46_stats = sStat(node_46->path);
  node_46_stats.st_uid++;
  mustHaveRegularStats(node_46, &metadata->backup_history[1], node_46_stats, 9, (uint8_t *)"Test file", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 47);
  mustHaveRegularStat(node_7, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(node_9, &metadata->current_backup, 12, (uint8_t *)"This is test", 0);
  mustHaveRegularStat(node_10, &metadata->current_backup, 11, (uint8_t *)"GID and UID", 0);
  mustHaveRegularStat(node_11, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_12, &metadata->current_backup, 14, (uint8_t *)"a short string", 0);
  mustHaveRegularStat(node_21, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(node_22, &metadata->current_backup, 1200, data_d_hash, 0);
  mustHaveRegularStat(node_23, &metadata->current_backup, 144, nested_1_hash, 0);
  mustHaveRegularStat(node_24, &metadata->current_backup, 63, node_24_hash, 0);
  mustHaveRegularStat(node_26, &metadata->current_backup, 22, node_26_hash, 0);
  mustHaveRegularStat(node_27, &metadata->current_backup, 21, nb_manual_b_hash, 0);
  mustHaveRegularStat(node_28, &metadata->current_backup, 2124, node_28_hash, 0);
  mustHaveRegularStat(node_29, &metadata->current_backup, 1200, node_29_hash, 0);
  mustHaveRegularStat(node_30, &metadata->current_backup, 400, three_hash, 0);
  mustHaveRegularStat(node_31, &metadata->current_backup, 2100, super_hash, 0);
  mustHaveRegularStat(node_32, &metadata->current_backup, 13, (uint8_t *)"A small file.", 0);
  mustHaveRegularStat(node_34, &metadata->current_backup, 15, (uint8_t *)"some dummy text", 0);
  mustHaveRegularStat(node_35, &metadata->current_backup, 1, (uint8_t *)"?", 0);
  mustHaveRegularStat(node_36, &metadata->current_backup, 11, (uint8_t *)"Nano Backup", 0);
  mustHaveRegularStat(node_37, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_38, &metadata->current_backup, 1, (uint8_t *)"@", 0);
  mustHaveRegularStat(node_39, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_40, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_41, &metadata->current_backup, 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_42, &metadata->current_backup, 518, node_42_hash, 0);
  mustHaveRegularStat(node_43, &metadata->current_backup, 12, (uint8_t *)"Large\nLarge\n", 0);
  mustHaveRegularStat(node_44, &metadata->current_backup, 20, (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  mustHaveRegularStat(node_45, &metadata->current_backup, 21, node_45_hash, 0);
  mustHaveRegularStat(node_46, &metadata->current_backup, 615, node_46_hash, 0);

  /* Assert that the previous states got left unmodified. */
  mustHaveRegularStats(node_7, &metadata->backup_history[1], node_7_stats, 400, three_hash, 0);
  mustHaveRegularStats(node_9, &metadata->backup_history[1], node_9_stats, 15, (uint8_t *)"This is a file\n", 0);
  mustHaveRegularStats(node_10, &metadata->backup_history[1], node_10_stats, 11, (uint8_t *)"GID and UID", 0);
  mustHaveRegularStats(node_11, &metadata->backup_history[1], node_11_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_12, &metadata->backup_history[1], node_12_stats, 84, some_file_hash, 0);
  mustHaveRegularStats(node_21, &metadata->backup_history[1], node_21_stats, 2100, super_hash, 0);
  mustHaveRegularStats(node_22, &metadata->backup_history[1], node_22_stats, 1200, data_d_hash, 0);
  mustHaveRegularStats(node_23, &metadata->backup_history[1], node_23_stats, 144, nested_1_hash, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[1], 56, nested_2_hash, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);
  mustHaveRegularStats(node_26, &metadata->backup_history[1], node_26_stats, 24, nb_a_abc_1_hash, 0);
  mustHaveRegularStats(node_27, &metadata->backup_history[1], node_27_stats, 21, nb_manual_b_hash, 0);
  mustHaveRegularStats(node_28, &metadata->backup_history[1], node_28_stats, 2123, bin_hash, 0);
  mustHaveRegularStats(node_29, &metadata->backup_history[1], node_29_stats, 1200, bin_c_1_hash, 0);
  mustHaveRegularStats(node_30, &metadata->backup_history[1], node_30_stats, 400, three_hash, 0);
  mustHaveRegularStats(node_31, &metadata->backup_history[1], node_31_stats, 2100, super_hash, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);
  mustHaveRegularStats(node_34, &metadata->backup_history[1], node_34_stats, 15, (uint8_t *)"Some dummy text", 0);
  mustHaveRegularStats(node_35, &metadata->backup_history[1], node_35_stats, 12, (uint8_t *)"abcdefghijkl", 0);
  mustHaveRegularStats(node_36, &metadata->backup_history[1], node_36_stats, 11, (uint8_t *)"Nano Backup", 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[1], 56, nested_2_hash, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[1], 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_39, &metadata->backup_history[1], node_39_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_40, &metadata->backup_history[1], node_40_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_41, &metadata->backup_history[1], node_41_stats, 11, (uint8_t *)"random file", 0);
  mustHaveRegularStats(node_42, &metadata->backup_history[1], node_42_stats, 0, (uint8_t *)"", 0);
  mustHaveRegularStats(node_43, &metadata->backup_history[1], node_43_stats, 1200, data_d_hash, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[1], 144, nested_1_hash, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[1], 10, (uint8_t *)"Small file", 0);
  mustHaveRegularStats(node_46, &metadata->backup_history[1], node_46_stats, 9, (uint8_t *)"Test file", 0);
}

/** Tests the metadata written by the previous phase and cleans up. */
static void trackPostDetectionTest(SearchNode *track_detection_node)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad(strWrap("tmp/repo/metadata"));
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(14), cwd_depth() + 47);
  checkHistPoint(metadata, 1, 1, phase_timestamps(12), 47);
  initiateBackup(metadata, track_detection_node);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 2);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 49);
  checkHistPoint(metadata, 0, 0, phase_timestamps(14), 45);
  checkHistPoint(metadata, 1, 1, phase_timestamps(12), 47);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 40);

  PathNode *node_0 = findSubnode(files, "0", BH_unchanged, BPOL_track, 2, 1);
  mustHaveDirectoryStat(node_0, &metadata->backup_history[0]);
  struct stat node_0_stats = sStat(node_0->path);
  node_0_stats.st_uid++;
  mustHaveDirectoryStats(node_0, &metadata->backup_history[1], node_0_stats);

  PathNode *node_1 = findSubnode(node_0, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_1, &metadata->backup_history[0]);
  struct stat node_1_stats = sStat(node_1->path);
  node_1_stats.st_gid++;
  mustHaveDirectoryStats(node_1, &metadata->backup_history[1], node_1_stats);

  PathNode *node_2 = findSubnode(files, "2", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_2, &metadata->backup_history[0]);
  struct stat node_2_stats = sStat(node_2->path);
  node_2_stats.st_mode++;
  mustHaveDirectoryStats(node_2, &metadata->backup_history[1], node_2_stats);

  PathNode *node_3 = findSubnode(files, "3", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_3, &metadata->backup_history[0]);
  struct stat node_3_stats = sStat(node_3->path);
  node_3_stats.st_mtime++;
  mustHaveDirectoryStats(node_3, &metadata->backup_history[1], node_3_stats);

  PathNode *node_4 = findSubnode(files, "4", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_4, &metadata->backup_history[0]);
  struct stat node_4_stats = sStat(node_4->path);
  node_4_stats.st_mode++;
  node_4_stats.st_mtime++;
  mustHaveDirectoryStats(node_4, &metadata->backup_history[1], node_4_stats);

  PathNode *node_5 = findSubnode(files, "5", BH_unchanged, BPOL_track, 2, 2);
  mustHaveDirectoryStat(node_5, &metadata->backup_history[0]);
  struct stat node_5_stats = sStat(node_5->path);
  node_5_stats.st_uid++;
  node_5_stats.st_mode++;
  mustHaveDirectoryStats(node_5, &metadata->backup_history[1], node_5_stats);

  PathNode *node_6 = findSubnode(node_5, "6", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_6, &metadata->backup_history[0], "/dev/null");
  struct stat node_6_stats = sLStat(node_6->path);
  node_6_stats.st_uid++;
  mustHaveSymlinkStats(node_6, &metadata->backup_history[1], node_6_stats, "/dev/non-existing");

  PathNode *node_7 = findSubnode(node_5, "7", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_7, &metadata->backup_history[0], 400, three_hash, 0);
  struct stat node_7_stats = sStat(node_7->path);
  node_7_stats.st_uid++;
  mustHaveRegularStats(node_7, &metadata->backup_history[1], node_7_stats, 400, three_hash, 0);

  PathNode *node_8 = findSubnode(files, "8", BH_unchanged, BPOL_track, 2, 4);
  mustHaveDirectoryStat(node_8, &metadata->backup_history[0]);
  struct stat node_8_stats = sStat(node_8->path);
  node_8_stats.st_gid++;
  node_8_stats.st_mtime++;
  mustHaveDirectoryStats(node_8, &metadata->backup_history[1], node_8_stats);

  PathNode *node_9 = findSubnode(node_8, "9", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_9, &metadata->backup_history[0], 12, (uint8_t *)"This is test", 0);
  struct stat node_9_stats = sStat(node_9->path);
  node_9_stats.st_uid++;
  mustHaveRegularStats(node_9, &metadata->backup_history[1], node_9_stats, 15, (uint8_t *)"This is a file\n", 0);

  PathNode *node_10 = findSubnode(node_8, "10", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_10, &metadata->backup_history[0], 11, (uint8_t *)"GID and UID", 0);
  struct stat node_10_stats = sStat(node_10->path);
  node_10_stats.st_mtime++;
  mustHaveRegularStats(node_10, &metadata->backup_history[1], node_10_stats, 11, (uint8_t *)"GID and UID", 0);

  PathNode *node_11 = findSubnode(node_8, "11", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_11, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_11_stats = sStat(node_11->path);
  node_11_stats.st_uid++;
  node_11_stats.st_mode++;
  mustHaveRegularStats(node_11, &metadata->backup_history[1], node_11_stats, 0, (uint8_t *)"", 0);

  PathNode *node_12 = findSubnode(node_8, "12", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_12, &metadata->backup_history[0], 14, (uint8_t *)"a short string", 0);
  struct stat node_12_stats = sStat(node_12->path);
  node_12_stats.st_gid++;
  node_12_stats.st_mode++;
  mustHaveRegularStats(node_12, &metadata->backup_history[1], node_12_stats, 84, some_file_hash, 0);

  PathNode *node_13 = findSubnode(files, "13", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_13, &metadata->backup_history[0]);
  struct stat node_13_stats = sStat(node_13->path);
  node_13_stats.st_gid++;
  node_13_stats.st_mode++;
  node_13_stats.st_mtime++;
  mustHaveDirectoryStats(node_13, &metadata->backup_history[1], node_13_stats);

  PathNode *node_14 = findSubnode(files, "14", BH_unchanged, BPOL_track, 2, 0);
  mustHaveDirectoryStat(node_14, &metadata->backup_history[0]);
  struct stat node_14_stats = sStat(node_14->path);
  node_14_stats.st_uid++;
  node_14_stats.st_mtime++;
  mustHaveDirectoryStats(node_14, &metadata->backup_history[1], node_14_stats);

  PathNode *node_15 = findSubnode(files, "15", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_15, &metadata->backup_history[0], "uid changing symlink");
  struct stat node_15_stats = sLStat(node_15->path);
  node_15_stats.st_uid++;
  mustHaveSymlinkStats(node_15, &metadata->backup_history[1], node_15_stats, "uid changing symlink");

  PathNode *node_16 = findSubnode(files, "16", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_16, &metadata->backup_history[0], "gid changing symlink");
  struct stat node_16_stats = sLStat(node_16->path);
  node_16_stats.st_gid++;
  mustHaveSymlinkStats(node_16, &metadata->backup_history[1], node_16_stats, "gid changing symlink");

  PathNode *node_17 = findSubnode(files, "17", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[0], "symlink-content");
  mustHaveSymlinkLStat(node_17, &metadata->backup_history[1], "symlink content");

  PathNode *node_18 = findSubnode(files, "18", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[0], "symlink content string");
  mustHaveSymlinkLStat(node_18, &metadata->backup_history[1], "symlink content");

  PathNode *node_19 = findSubnode(files, "19", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_19, &metadata->backup_history[0], "uid + content");
  struct stat node_19_stats = sLStat(node_19->path);
  node_19_stats.st_gid++;
  mustHaveSymlinkStats(node_19, &metadata->backup_history[1], node_19_stats, "gid + content");

  PathNode *node_20 = findSubnode(files, "20", BH_unchanged, BPOL_track, 2, 0);
  mustHaveSymlinkLStat(node_20, &metadata->backup_history[0], "content, uid, gid ");
  struct stat node_20_stats = sLStat(node_20->path);
  node_20_stats.st_uid++;
  node_20_stats.st_gid++;
  mustHaveSymlinkStats(node_20, &metadata->backup_history[1], node_20_stats, "content, uid, gid");

  PathNode *node_21 = findSubnode(files, "21", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_21, &metadata->backup_history[0], 2100, super_hash, 0);
  struct stat node_21_stats = sStat(node_21->path);
  node_21_stats.st_gid++;
  mustHaveRegularStats(node_21, &metadata->backup_history[1], node_21_stats, 2100, super_hash, 0);

  PathNode *node_22 = findSubnode(files, "22", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_22, &metadata->backup_history[0], 1200, data_d_hash, 0);
  struct stat node_22_stats = sStat(node_22->path);
  node_22_stats.st_mode++;
  mustHaveRegularStats(node_22, &metadata->backup_history[1], node_22_stats, 1200, data_d_hash, 0);

  PathNode *node_23 = findSubnode(files, "23", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_23, &metadata->backup_history[0], 144, nested_1_hash, 0);
  struct stat node_23_stats = sStat(node_23->path);
  node_23_stats.st_mtime++;
  mustHaveRegularStats(node_23, &metadata->backup_history[1], node_23_stats, 144, nested_1_hash, 0);

  PathNode *node_24 = findSubnode(files, "24", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[0], 63, node_24_hash, 0);
  mustHaveRegularStat(node_24, &metadata->backup_history[1], 56, nested_2_hash, 0);

  PathNode *node_25 = findSubnode(files, "25", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_25, &metadata->backup_history[1], 42, test_c_hash, 0);

  PathNode *node_26 = findSubnode(files, "26", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_26, &metadata->backup_history[0], 22, node_26_hash, 0);
  struct stat node_26_stats = sStat(node_26->path);
  node_26_stats.st_gid++;
  mustHaveRegularStats(node_26, &metadata->backup_history[1], node_26_stats, 24, nb_a_abc_1_hash, 0);

  PathNode *node_27 = findSubnode(files, "27", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_27, &metadata->backup_history[0], 21, nb_manual_b_hash, 0);
  struct stat node_27_stats = sStat(node_27->path);
  node_27_stats.st_mode++;
  mustHaveRegularStats(node_27, &metadata->backup_history[1], node_27_stats, 21, nb_manual_b_hash, 0);

  PathNode *node_28 = findSubnode(files, "28", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_28, &metadata->backup_history[0], 2124, node_28_hash, 0);
  struct stat node_28_stats = sStat(node_28->path);
  node_28_stats.st_mtime++;
  mustHaveRegularStats(node_28, &metadata->backup_history[1], node_28_stats, 2123, bin_hash, 0);

  PathNode *node_29 = findSubnode(files, "29", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_29, &metadata->backup_history[0], 1200, node_29_hash, 0);
  struct stat node_29_stats = sStat(node_29->path);
  node_29_stats.st_uid++;
  node_29_stats.st_mtime++;
  mustHaveRegularStats(node_29, &metadata->backup_history[1], node_29_stats, 1200, bin_c_1_hash, 0);

  PathNode *node_30 = findSubnode(files, "30", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_30, &metadata->backup_history[0], 400, three_hash, 0);
  struct stat node_30_stats = sStat(node_30->path);
  node_30_stats.st_uid++;
  node_30_stats.st_mode++;
  node_30_stats.st_mtime++;
  mustHaveRegularStats(node_30, &metadata->backup_history[1], node_30_stats, 400, three_hash, 0);

  PathNode *node_31 = findSubnode(files, "31", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_31, &metadata->backup_history[0], 2100, super_hash, 0);
  struct stat node_31_stats = sStat(node_31->path);
  node_31_stats.st_uid++;
  node_31_stats.st_gid++;
  mustHaveRegularStats(node_31, &metadata->backup_history[1], node_31_stats, 2100, super_hash, 0);

  PathNode *node_32 = findSubnode(files, "32", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[0], 13, (uint8_t *)"A small file.", 0);
  mustHaveRegularStat(node_32, &metadata->backup_history[1], 12, (uint8_t *)"A small file", 0);

  PathNode *node_33 = findSubnode(files, "33", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(node_33, &metadata->backup_history[1], 12, (uint8_t *)"Another file", 0);

  PathNode *node_34 = findSubnode(files, "34", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_34, &metadata->backup_history[0], 15, (uint8_t *)"some dummy text", 0);
  struct stat node_34_stats = sStat(node_34->path);
  node_34_stats.st_mtime++;
  mustHaveRegularStats(node_34, &metadata->backup_history[1], node_34_stats, 15, (uint8_t *)"Some dummy text", 0);

  PathNode *node_35 = findSubnode(files, "35", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_35, &metadata->backup_history[0], 1, (uint8_t *)"?", 0);
  struct stat node_35_stats = sStat(node_35->path);
  node_35_stats.st_mode++;
  mustHaveRegularStats(node_35, &metadata->backup_history[1], node_35_stats, 12, (uint8_t *)"abcdefghijkl", 0);

  PathNode *node_36 = findSubnode(files, "36", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_36, &metadata->backup_history[0], 11, (uint8_t *)"Nano Backup", 0);
  struct stat node_36_stats = sStat(node_36->path);
  node_36_stats.st_gid++;
  node_36_stats.st_mode++;
  mustHaveRegularStats(node_36, &metadata->backup_history[1], node_36_stats, 11, (uint8_t *)"Nano Backup", 0);

  PathNode *node_37 = findSubnode(files, "37", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  mustHaveRegularStat(node_37, &metadata->backup_history[1], 56, nested_2_hash, 0);

  PathNode *node_38 = findSubnode(files, "38", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[0], 1, (uint8_t *)"@", 0);
  mustHaveRegularStat(node_38, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *node_39 = findSubnode(files, "39", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_39, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_39_stats = sStat(node_39->path);
  node_39_stats.st_gid++;
  mustHaveRegularStats(node_39, &metadata->backup_history[1], node_39_stats, 0, (uint8_t *)"", 0);

  PathNode *node_40 = findSubnode(files, "40", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_40, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_40_stats = sStat(node_40->path);
  node_40_stats.st_mtime++;
  mustHaveRegularStats(node_40, &metadata->backup_history[1], node_40_stats, 0, (uint8_t *)"", 0);

  PathNode *node_41 = findSubnode(files, "41", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_41, &metadata->backup_history[0], 0, (uint8_t *)"", 0);
  struct stat node_41_stats = sStat(node_41->path);
  node_41_stats.st_mode++;
  mustHaveRegularStats(node_41, &metadata->backup_history[1], node_41_stats, 11, (uint8_t *)"random file", 0);

  PathNode *node_42 = findSubnode(files, "42", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_42, &metadata->backup_history[0], 518, node_42_hash, 0);
  struct stat node_42_stats = sStat(node_42->path);
  node_42_stats.st_gid++;
  mustHaveRegularStats(node_42, &metadata->backup_history[1], node_42_stats, 0, (uint8_t *)"", 0);

  PathNode *node_43 = findSubnode(files, "43", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_43, &metadata->backup_history[0], 12, (uint8_t *)"Large\nLarge\n", 0);
  struct stat node_43_stats = sStat(node_43->path);
  node_43_stats.st_mtime++;
  mustHaveRegularStats(node_43, &metadata->backup_history[1], node_43_stats, 1200, data_d_hash, 0);

  PathNode *node_44 = findSubnode(files, "44", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[0], 20, (uint8_t *)"QQQQQQQQQQQQQQQQQQQQ", 0);
  mustHaveRegularStat(node_44, &metadata->backup_history[1], 144, nested_1_hash, 0);

  PathNode *node_45 = findSubnode(files, "45", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[0], 21, node_45_hash, 0);
  mustHaveRegularStat(node_45, &metadata->backup_history[1], 10, (uint8_t *)"Small file", 0);

  PathNode *node_46 = findSubnode(files, "46", BH_unchanged, BPOL_track, 2, 0);
  mustHaveRegularStat(node_46, &metadata->backup_history[0], 615, node_46_hash, 0);
  struct stat node_46_stats = sStat(node_46->path);
  node_46_stats.st_uid++;
  mustHaveRegularStats(node_46, &metadata->backup_history[1], node_46_stats, 9, (uint8_t *)"Test file", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 47);
}

int main(void)
{
  initBackupCommon(1);

  testGroupStart("detecting changes in nodes with no policy");
  SearchNode *none_detection_node = searchTreeLoad(strWrap("generated-config-files/backup-phase-17.txt"));

  initNoneChangeTest(none_detection_node);
  modifyNoneChangeTest(none_detection_node);
  changeNoneChangeTest(none_detection_node);
  postNoneChangeTest(none_detection_node);
  testGroupEnd();

  testGroupStart("detecting changes in copied nodes");
  SearchNode *copy_detection_node = searchTreeLoad(strWrap("generated-config-files/change-detection-copy.txt"));

  initChangeDetectionTest(copy_detection_node, BPOL_copy);
  modifyChangeDetectionTest(copy_detection_node, BPOL_copy);
  changeDetectionTest(copy_detection_node, BPOL_copy);
  postDetectionTest(copy_detection_node, BPOL_copy);
  testGroupEnd();

  testGroupStart("detecting changes in mirrored nodes");
  SearchNode *mirror_detection_node =
    searchTreeLoad(strWrap("generated-config-files/change-detection-mirror.txt"));

  initChangeDetectionTest(mirror_detection_node, BPOL_mirror);
  modifyChangeDetectionTest(mirror_detection_node, BPOL_mirror);
  changeDetectionTest(mirror_detection_node, BPOL_mirror);
  postDetectionTest(mirror_detection_node, BPOL_mirror);
  testGroupEnd();

  testGroupStart("detecting changes in tracked nodes");
  SearchNode *track_detection_node = searchTreeLoad(strWrap("generated-config-files/change-detection-track.txt"));

  initChangeDetectionTest(track_detection_node, BPOL_track);
  modifyChangeDetectionTest(track_detection_node, BPOL_track);
  trackChangeDetectionTest(track_detection_node);
  trackPostDetectionTest(track_detection_node);
  testGroupEnd();
}
