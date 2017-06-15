/** @file
  Tests handling of policy changes.
*/

#include "backup.h"

#include "test.h"
#include "metadata.h"
#include "search-tree.h"
#include "test-common.h"
#include "backup-common.h"
#include "safe-wrappers.h"

/** Prepares policy change test from BPOL_none. */
static void policyChangeFromNoneInit(SearchNode *change_from_none_init)
{
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/b");
  makeDir("tmp/files/c");
  makeDir("tmp/files/d");
  makeDir("tmp/files/e");
  makeDir("tmp/files/f");
  makeDir("tmp/files/g");
  makeDir("tmp/files/h");
  makeDir("tmp/files/h/1");
  makeDir("tmp/files/h/3");
  generateFile("tmp/files/a/1",   "test file", 1);
  generateFile("tmp/files/b/1",   "_123_",     1);
  generateFile("tmp/files/c/1",   "abcdef",    1);
  generateFile("tmp/files/d/1",   "foo-bar",   1);
  generateFile("tmp/files/e/1",   "SomeFile",  1);
  generateFile("tmp/files/f/1",   "somefile",  1);
  generateFile("tmp/files/g/1",   "1 + 1 = 2", 1);
  generateFile("tmp/files/h/1/2", ".",         5);
  generateFile("tmp/files/h/3/4", "%",         11);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_from_none_init);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 21);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth() + 21);

  /* Populate stat cache. */
  PathNode *files = findFilesNode(metadata, BH_added, 8);

  PathNode *b = findSubnode(files, "b", BH_added, BPOL_none, 1, 1);
  cachedStat(b->path, sStat);
  cachedStat(findSubnode(b, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *d = findSubnode(files, "d", BH_added, BPOL_none, 1, 1);
  cachedStat(d->path, sStat);
  cachedStat(findSubnode(d, "1", BH_added, BPOL_track, 1, 0)->path, sStat);

  PathNode *f = findSubnode(files, "f", BH_added, BPOL_none, 1, 1);
  cachedStat(f->path, sStat);
  cachedStat(findSubnode(f, "1", BH_added, BPOL_track, 1, 0)->path, sStat);

  PathNode *g = findSubnode(files, "g", BH_added, BPOL_none, 1, 1);
  cachedStat(g->path, sStat);
  cachedStat(findSubnode(g, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *h = findSubnode(files, "h", BH_added, BPOL_none, 1, 2);
  cachedStat(h->path, sStat);
  PathNode *h_1 = findSubnode(h, "1", BH_added, BPOL_copy, 1, 1);
  cachedStat(h_1->path, sStat);
  cachedStat(findSubnode(h_1, "2", BH_added, BPOL_track, 1, 0)->path, sStat);
  PathNode *h_3 = findSubnode(h, "3", BH_added, BPOL_mirror, 1, 1);
  cachedStat(h_3->path, sStat);
  cachedStat(findSubnode(h_3, "4", BH_added, BPOL_track, 1, 0)->path, sStat);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove some files. */
  removePath("tmp/files/b/1");
  removePath("tmp/files/b");
  removePath("tmp/files/d/1");
  removePath("tmp/files/d");
  removePath("tmp/files/f/1");
  removePath("tmp/files/f");
  removePath("tmp/files/g/1");
  removePath("tmp/files/g");
  removePath("tmp/files/h/1/2");
  removePath("tmp/files/h/1");
  removePath("tmp/files/h/3/4");
  removePath("tmp/files/h/3");
  removePath("tmp/files/h");

  /* Initiate another backup. */
  metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 21);
  initiateBackup(metadata, change_from_none_init);

  /* Check the other backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 5);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth() + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 16);

  /* Finish the other backup. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Finishes policy change test from BPOL_none. */
static void policyChangeFromNoneChange(SearchNode *change_from_none_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 21);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 5);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 16);
  initiateBackup(metadata, change_from_none_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 6);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 19);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 3);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 14);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 8);

  PathNode *a = findSubnode(files, "a", BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(a, &metadata->backup_history[0]);
  PathNode *a_1 = findSubnode(a, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(a_1, &metadata->backup_history[1], 9, (uint8_t *)"test file", 0);

  PathNode *b = findSubnode(files, "b", BH_removed | BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b, &metadata->backup_history[1]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[1], 5, (uint8_t *)"_123_", 0);

  PathNode *c = findSubnode(files, "c", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[0]);
  PathNode *c_1 = findSubnode(c, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(c_1, &metadata->backup_history[1], 6, (uint8_t *)"abcdef", 0);

  PathNode *d = findSubnode(files, "d", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);
  PathNode *d_1 = findSubnode(d, "1", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(d_1, &metadata->backup_history[1], 7, (uint8_t *)"foo-bar", 0);

  PathNode *e = findSubnode(files, "e", BH_policy_changed, BPOL_track, 1, 1);
  mustHaveDirectoryStat(e, &metadata->backup_history[0]);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[1], 8, (uint8_t *)"SomeFile", 0);

  PathNode *f = findSubnode(files, "f", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(f, &metadata->current_backup);
  mustHaveDirectoryCached(f, &metadata->backup_history[1]);
  PathNode *f_1 = findSubnode(f, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(f_1, &metadata->current_backup);
  mustHaveRegularCached(f_1, &metadata->backup_history[1], 8, (uint8_t *)"somefile", 0);

  PathNode *g = findSubnode(files, "g", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(g, &metadata->current_backup);
  mustHaveDirectoryCached(g, &metadata->backup_history[1]);
  PathNode *g_1 = findSubnode(g, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(g_1, &metadata->backup_history[1], 9, (uint8_t *)"1 + 1 = 2", 0);

  PathNode *h = findSubnode(files, "h", BH_removed | BH_policy_changed, BPOL_track, 2, 2);
  mustHaveNonExisting(h, &metadata->current_backup);
  mustHaveDirectoryCached(h, &metadata->backup_history[1]);
  PathNode *h_1 = findSubnode(h, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(h_1, &metadata->backup_history[1]);
  PathNode *h_2 = findSubnode(h_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_2, &metadata->backup_history[1], 5, (uint8_t *)".....", 0);
  PathNode *h_3 = findSubnode(h, "3", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(h_3, &metadata->backup_history[1]);
  PathNode *h_4 = findSubnode(h_3, "4", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_4, &metadata->backup_history[1], 11, (uint8_t *)"%%%%%%%%%%%", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Checks the metadata written by the previous test and cleans up. */
static void policyChangeFromNonePost(SearchNode *change_from_none_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 19);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 6);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 3);
  checkHistPoint(metadata, 2, 2, phase_timestamps(backup_counter() - 3), 14);
  initiateBackup(metadata, change_from_none_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 2);
  assert_true(metadata->backup_history_length == 3);
  assert_true(metadata->total_path_count == cwd_depth() + 19);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 4);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 3);
  checkHistPoint(metadata, 2, 2, phase_timestamps(backup_counter() - 3), 14);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 7);

  PathNode *a = findSubnode(files, "a", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(a, &metadata->backup_history[1]);
  PathNode *a_1 = findSubnode(a, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(a_1, &metadata->backup_history[2], 9, (uint8_t *)"test file", 0);

  PathNode *b = findSubnode(files, "b", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(b, &metadata->backup_history[2]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[2], 5, (uint8_t *)"_123_", 0);

  PathNode *c = findSubnode(files, "c", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(c_1, &metadata->backup_history[2], 6, (uint8_t *)"abcdef", 0);

  PathNode *e = findSubnode(files, "e", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryStat(e, &metadata->backup_history[1]);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[2], 8, (uint8_t *)"SomeFile", 0);

  PathNode *f = findSubnode(files, "f", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(f, &metadata->backup_history[0]);
  mustHaveDirectoryCached(f, &metadata->backup_history[2]);
  PathNode *f_1 = findSubnode(f, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(f_1, &metadata->backup_history[0]);
  mustHaveRegularCached(f_1, &metadata->backup_history[2], 8, (uint8_t *)"somefile", 0);

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(g, &metadata->backup_history[0]);
  mustHaveDirectoryCached(g, &metadata->backup_history[2]);
  PathNode *g_1 = findSubnode(g, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(g_1, &metadata->backup_history[2], 9, (uint8_t *)"1 + 1 = 2", 0);

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_track, 2, 2);
  mustHaveNonExisting(h, &metadata->backup_history[0]);
  mustHaveDirectoryCached(h, &metadata->backup_history[2]);
  PathNode *h_1 = findSubnode(h, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(h_1, &metadata->backup_history[2]);
  PathNode *h_2 = findSubnode(h_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_2, &metadata->backup_history[2], 5, (uint8_t *)".....", 0);
  PathNode *h_3 = findSubnode(h, "3", BH_removed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(h_3, &metadata->backup_history[2]);
  PathNode *h_4 = findSubnode(h_3, "4", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(h_4, &metadata->backup_history[2], 11, (uint8_t *)"%%%%%%%%%%%", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Copy counterpart to policyChangeFromNoneInit(). */
static void policyChangeFromCopyInit(SearchNode *change_from_copy_init)
{
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/c");
  makeDir("tmp/files/d");
  makeDir("tmp/files/e");
  makeDir("tmp/files/f");
  makeDir("tmp/files/g");
  makeDir("tmp/files/g/1");
  makeDir("tmp/files/g/1/2");
  makeDir("tmp/files/i");
  makeDir("tmp/files/i/1");
  makeDir("tmp/files/j");
  makeDir("tmp/files/l");
  makeDir("tmp/files/n");
  makeDir("tmp/files/o");
  makeDir("tmp/files/q");
  makeDir("tmp/files/r");
  makeDir("tmp/files/s");
  makeDir("tmp/files/s/2");
  generateFile("tmp/files/a/1",   "file a content", 1);
  generateFile("tmp/files/b",     "CONTENT",        1);
  generateFile("tmp/files/c/1",   "foo",            1);
  generateFile("tmp/files/e/1",   "nano backup",    1);
  generateFile("tmp/files/f/1",   "BackupBackup",   1);
  generateFile("tmp/files/f/2",   "Lorem Ipsum",    1);
  generateFile("tmp/files/j/1",   "random string",  1);
  generateFile("tmp/files/k",     "another string", 1);
  generateFile("tmp/files/l/1",   "abc",            1);
  generateFile("tmp/files/l/2",   "xyz",            1);
  generateFile("tmp/files/l/3",   "123",            1);
  generateFile("tmp/files/m",     "",               0);
  generateFile("tmp/files/n/1",   "[]",             3);
  generateFile("tmp/files/o/1",   "=",              12);
  generateFile("tmp/files/p",     "FILE_CONTENT",   1);
  generateFile("tmp/files/q/1",   "_CONTENT_",      1);
  generateFile("tmp/files/q/2",   "_FILE_",         1);
  generateFile("tmp/files/r/1",   "!@#$%^&*()_+",   1);
  generateFile("tmp/files/r/2",   "_backup_",       1);
  generateFile("tmp/files/s/1",   "abcdefghijkl",   1);
  generateFile("tmp/files/s/2/3", "ABCDEF",         1);
  makeSymlink("/dev/null", "tmp/files/h");

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_from_copy_init);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 42);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth() + 42);

  /* Populate stat cache. */
  PathNode *files = findFilesNode(metadata, BH_added, 19);

  PathNode *c = findSubnode(files, "c", BH_added, BPOL_copy, 1, 1);
  cachedStat(c->path, sStat);
  cachedStat(findSubnode(c, "1", BH_added, BPOL_copy, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "d", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *f = findSubnode(files, "f", BH_added, BPOL_copy, 1, 2);
  cachedStat(f->path, sStat);
  cachedStat(findSubnode(f, "1", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(f, "2", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *j = findSubnode(files, "j", BH_added, BPOL_copy, 1, 1);
  cachedStat(j->path, sStat);
  cachedStat(findSubnode(j, "1", BH_added, BPOL_copy, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "k", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *l = findSubnode(files, "l", BH_added, BPOL_copy, 1, 3);
  cachedStat(l->path, sStat);
  cachedStat(findSubnode(l, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);
  cachedStat(findSubnode(l, "2", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(l, "3", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *o = findSubnode(files, "o", BH_added, BPOL_copy, 1, 1);
  cachedStat(o->path, sStat);
  cachedStat(findSubnode(o, "1", BH_added, BPOL_copy, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "p", BH_added, BPOL_copy, 1, 0)->path, sStat);

  PathNode *r = findSubnode(files, "r", BH_added, BPOL_copy, 1, 2);
  cachedStat(r->path, sStat);
  cachedStat(findSubnode(r, "1", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(r, "2", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *s = findSubnode(files, "s", BH_added, BPOL_copy, 1, 2);
  cachedStat(s->path, sStat);
  cachedStat(findSubnode(s, "1", BH_added, BPOL_track, 1, 0)->path, sStat);
  PathNode *s_2 = findSubnode(s, "2", BH_added, BPOL_copy, 1, 1);
  cachedStat(s_2->path, sStat);
  cachedStat(findSubnode(s_2, "3", BH_added, BPOL_track, 1, 0)->path, sStat);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove some files. */
  removePath("tmp/files/c/1");
  removePath("tmp/files/c");
  removePath("tmp/files/d");
  removePath("tmp/files/f/2");
  removePath("tmp/files/f/1");
  removePath("tmp/files/f");
  removePath("tmp/files/j/1");
  removePath("tmp/files/j");
  removePath("tmp/files/k");
  removePath("tmp/files/l/3");
  removePath("tmp/files/l/2");
  removePath("tmp/files/l/1");
  removePath("tmp/files/l");
  removePath("tmp/files/p");
  removePath("tmp/files/r/2");
  removePath("tmp/files/r/1");
  removePath("tmp/files/r");

  /* Initiate another backup. */
  metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 42);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 42);
  initiateBackup(metadata, change_from_copy_init);

  /* Check the other backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 2);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth() + 42);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 40);

  /* Finish the other backup. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Copy counterpart to policyChangeFromNoneChange(). */
static void policyChangeFromCopyChange(SearchNode *change_from_copy_final)
{
  /* Remove various files directly before the initiation. */
  removePath("tmp/files/o/1");
  removePath("tmp/files/o");
  removePath("tmp/files/s/2/3");
  removePath("tmp/files/s/2");
  removePath("tmp/files/s/1");
  removePath("tmp/files/s");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 42);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 2);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 40);
  initiateBackup(metadata, change_from_copy_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 9);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 32);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 0);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 29);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 19);

  PathNode *a = findSubnode(files, "a", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *a_1 = findSubnode(a, "1", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveRegularStat(a_1, &metadata->current_backup, 14, (uint8_t *)"file a content", 0);

  PathNode *b = findSubnode(files, "b", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveRegularStat(b, &metadata->current_backup, 7, (uint8_t *)"CONTENT", 0);

  PathNode *c = findSubnode(files, "c", BH_removed | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(c_1, &metadata->backup_history[1], 3, (uint8_t *)"foo", 0);

  PathNode *d = findSubnode(files, "d", BH_removed | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);

  PathNode *e = findSubnode(files, "e", BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[1], 11, (uint8_t *)"nano backup", 0);

  PathNode *f = findSubnode(files, "f", BH_removed | BH_policy_changed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(f, &metadata->backup_history[1]);
  PathNode *f_1 = findSubnode(f, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(f_1, &metadata->backup_history[1], 12, (uint8_t *)"BackupBackup", 0);
  PathNode *f_2 = findSubnode(f, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(f_2, &metadata->backup_history[1], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *g = findSubnode(files, "g", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g, &metadata->backup_history[1]);
  PathNode *g_1 = findSubnode(g, "1", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g_1, &metadata->backup_history[1]);
  PathNode *g_2 = findSubnode(g_1, "2", BH_policy_changed, BPOL_mirror, 1, 0);
  mustHaveDirectoryStat(g_2, &metadata->backup_history[1]);

  PathNode *h = findSubnode(files, "h", BH_policy_changed, BPOL_mirror, 1, 0);
  mustHaveSymlinkLStat(h, &metadata->backup_history[1], "/dev/null");

  PathNode *i = findSubnode(files, "i", BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(i, &metadata->backup_history[1]);
  PathNode *i_1 = findSubnode(i, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(i_1, &metadata->backup_history[1]);

  PathNode *j = findSubnode(files, "j", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 1);
  mustHaveDirectoryCached(j, &metadata->backup_history[1]);
  PathNode *j_1 = findSubnode(j, "1", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(j_1, &metadata->backup_history[1], 13, (uint8_t *)"random string", 0);

  PathNode *k = findSubnode(files, "k", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(k, &metadata->backup_history[1], 14, (uint8_t *)"another string", 0);

  PathNode *l = findSubnode(files, "l", BH_not_part_of_repository | BH_policy_changed, BPOL_mirror, 1, 3);
  mustHaveDirectoryCached(l, &metadata->backup_history[1]);
  PathNode *l_1 = findSubnode(l, "1", BH_not_part_of_repository, BPOL_mirror, 1, 0);
  mustHaveRegularCached(l_1, &metadata->backup_history[1], 3, (uint8_t *)"abc", 0);
  PathNode *l_2 = findSubnode(l, "2", BH_not_part_of_repository, BPOL_track, 1, 0);
  mustHaveRegularCached(l_2, &metadata->backup_history[1], 3, (uint8_t *)"xyz", 0);
  PathNode *l_3 = findSubnode(l, "3", BH_not_part_of_repository, BPOL_copy, 1, 0);
  mustHaveRegularCached(l_3, &metadata->backup_history[1], 3, (uint8_t *)"123", 0);

  PathNode *m = findSubnode(files, "m", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(m, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *n = findSubnode(files, "n", BH_policy_changed, BPOL_track, 1, 1);
  mustHaveDirectoryStat(n, &metadata->backup_history[1]);
  PathNode *n_1 = findSubnode(n, "1", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(n_1, &metadata->backup_history[1], 6, (uint8_t *)"[][][]", 0);

  PathNode *o = findSubnode(files, "o", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(o, &metadata->current_backup);
  mustHaveDirectoryCached(o, &metadata->backup_history[1]);
  PathNode *o_1 = findSubnode(o, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(o_1, &metadata->backup_history[1], 12, (uint8_t *)"============", 0);

  PathNode *p = findSubnode(files, "p", BH_removed | BH_policy_changed, BPOL_track, 2, 0);
  mustHaveNonExisting(p, &metadata->current_backup);
  mustHaveRegularCached(p, &metadata->backup_history[1], 12, (uint8_t *)"FILE_CONTENT", 0);

  PathNode *q = findSubnode(files, "q", BH_policy_changed, BPOL_track, 1, 2);
  mustHaveDirectoryStat(q, &metadata->backup_history[1]);
  PathNode *q_1 = findSubnode(q, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(q_1, &metadata->backup_history[1], 9, (uint8_t *)"_CONTENT_", 0);
  PathNode *q_2 = findSubnode(q, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(q_2, &metadata->backup_history[1], 6, (uint8_t *)"_FILE_", 0);

  PathNode *r = findSubnode(files, "r", BH_removed | BH_policy_changed, BPOL_track, 2, 2);
  mustHaveNonExisting(r, &metadata->current_backup);
  mustHaveDirectoryCached(r, &metadata->backup_history[1]);
  PathNode *r_1 = findSubnode(r, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(r_1, &metadata->current_backup);
  mustHaveRegularCached(r_1, &metadata->backup_history[1], 12, (uint8_t *)"!@#$%^&*()_+", 0);
  PathNode *r_2 = findSubnode(r, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(r_2, &metadata->backup_history[1], 8, (uint8_t *)"_backup_", 0);

  PathNode *s = findSubnode(files, "s", BH_removed | BH_policy_changed, BPOL_track, 2, 2);
  mustHaveNonExisting(s, &metadata->current_backup);
  mustHaveDirectoryCached(s, &metadata->backup_history[1]);
  PathNode *s_1 = findSubnode(s, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(s_1, &metadata->current_backup);
  mustHaveRegularCached(s_1, &metadata->backup_history[1], 12, (uint8_t *)"abcdefghijkl", 0);
  PathNode *s_2 = findSubnode(s, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(s_2, &metadata->backup_history[1]);
  PathNode *s_3 = findSubnode(s_2, "3", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(s_3, &metadata->backup_history[1], 6, (uint8_t *)"ABCDEF", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove various files to prevent rediscovering. */
  removePath("tmp/files/a/1");
  removePath("tmp/files/a");
  removePath("tmp/files/b");
}

/** Copy counterpart to policyChangeFromNonePost(). */
static void policyChangeFromCopyPost(SearchNode *change_from_copy_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 32);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 9);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 3), 29);
  initiateBackup(metadata, change_from_copy_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 3);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 32);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 6);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 3), 29);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 14);

  PathNode *c = findSubnode(files, "c", BH_removed, BPOL_none, 1, 1);
  mustHaveDirectoryCached(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(c_1, &metadata->backup_history[1], 3, (uint8_t *)"foo", 0);

  PathNode *d = findSubnode(files, "d", BH_removed, BPOL_none, 1, 0);
  mustHaveDirectoryCached(d, &metadata->backup_history[1]);

  PathNode *e = findSubnode(files, "e", BH_unchanged, BPOL_none, 1, 1);
  mustHaveDirectoryStat(e, &metadata->current_backup);
  PathNode *e_1 = findSubnode(e, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(e_1, &metadata->backup_history[1], 11, (uint8_t *)"nano backup", 0);

  PathNode *f = findSubnode(files, "f", BH_removed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(f, &metadata->backup_history[1]);
  PathNode *f_1 = findSubnode(f, "1", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(f_1, &metadata->backup_history[1], 12, (uint8_t *)"BackupBackup", 0);
  PathNode *f_2 = findSubnode(f, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(f_2, &metadata->backup_history[1], 11, (uint8_t *)"Lorem Ipsum", 0);

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g, &metadata->backup_history[1]);
  PathNode *g_1 = findSubnode(g, "1", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(g_1, &metadata->backup_history[1]);
  PathNode *g_2 = findSubnode(g_1, "2", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveDirectoryStat(g_2, &metadata->backup_history[1]);

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveSymlinkLStat(h, &metadata->backup_history[1], "/dev/null");

  PathNode *i = findSubnode(files, "i", BH_unchanged, BPOL_mirror, 1, 1);
  mustHaveDirectoryStat(i, &metadata->backup_history[1]);
  PathNode *i_1 = findSubnode(i, "1", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveDirectoryStat(i_1, &metadata->backup_history[1]);

  PathNode *m = findSubnode(files, "m", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(m, &metadata->backup_history[1], 0, (uint8_t *)"", 0);

  PathNode *n = findSubnode(files, "n", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryStat(n, &metadata->backup_history[1]);
  PathNode *n_1 = findSubnode(n, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(n_1, &metadata->backup_history[1], 6, (uint8_t *)"[][][]", 0);

  PathNode *o = findSubnode(files, "o", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(o, &metadata->backup_history[0]);
  mustHaveDirectoryCached(o, &metadata->backup_history[1]);
  PathNode *o_1 = findSubnode(o, "1", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(o_1, &metadata->backup_history[1], 12, (uint8_t *)"============", 0);

  PathNode *p = findSubnode(files, "p", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(p, &metadata->backup_history[0]);
  mustHaveRegularCached(p, &metadata->backup_history[1], 12, (uint8_t *)"FILE_CONTENT", 0);

  PathNode *q = findSubnode(files, "q", BH_unchanged, BPOL_track, 1, 2);
  mustHaveDirectoryStat(q, &metadata->backup_history[1]);
  PathNode *q_1 = findSubnode(q, "1", BH_unchanged, BPOL_mirror, 1, 0);
  mustHaveRegularStat(q_1, &metadata->backup_history[1], 9, (uint8_t *)"_CONTENT_", 0);
  PathNode *q_2 = findSubnode(q, "2", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(q_2, &metadata->backup_history[1], 6, (uint8_t *)"_FILE_", 0);

  PathNode *r = findSubnode(files, "r", BH_unchanged, BPOL_track, 2, 2);
  mustHaveNonExisting(r, &metadata->backup_history[0]);
  mustHaveDirectoryCached(r, &metadata->backup_history[1]);
  PathNode *r_1 = findSubnode(r, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(r_1, &metadata->backup_history[0]);
  mustHaveRegularCached(r_1, &metadata->backup_history[1], 12, (uint8_t *)"!@#$%^&*()_+", 0);
  PathNode *r_2 = findSubnode(r, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(r_2, &metadata->backup_history[1], 8, (uint8_t *)"_backup_", 0);

  PathNode *s = findSubnode(files, "s", BH_unchanged, BPOL_track, 2, 2);
  mustHaveNonExisting(s, &metadata->backup_history[0]);
  mustHaveDirectoryCached(s, &metadata->backup_history[1]);
  PathNode *s_1 = findSubnode(s, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(s_1, &metadata->backup_history[0]);
  mustHaveRegularCached(s_1, &metadata->backup_history[1], 12, (uint8_t *)"abcdefghijkl", 0);
  PathNode *s_2 = findSubnode(s, "2", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(s_2, &metadata->backup_history[1]);
  PathNode *s_3 = findSubnode(s_2, "3", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(s_3, &metadata->backup_history[1], 6, (uint8_t *)"ABCDEF", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Mirror counterpart to policyChangeFromNoneInit(). */
static void policyChangeFromMirrorInit(SearchNode *change_from_mirror_init)
{
  resetStatCache();
  assertTmpIsCleared();
  makeDir("tmp/files/a");
  makeDir("tmp/files/a/1");
  makeDir("tmp/files/b");
  makeDir("tmp/files/c");
  makeDir("tmp/files/c/1");
  makeDir("tmp/files/e");
  makeDir("tmp/files/h");
  makeDir("tmp/files/i");
  makeDir("tmp/files/i/1");
  makeDir("tmp/files/i/3");
  makeDir("tmp/files/j");
  generateFile("tmp/files/a/1/2", "",              0);
  generateFile("tmp/files/b/1",   "random123",     1);
  generateFile("tmp/files/b/2",   "Foo-Barbar",    1);
  generateFile("tmp/files/c/1/2", "987654321",     1);
  generateFile("tmp/files/d",     "some text",     1);
  generateFile("tmp/files/e/1",   "tmp/files/e/1", 1);
  generateFile("tmp/files/f",     "... Files_e_1", 1);
  generateFile("tmp/files/g",     "",              0);
  generateFile("tmp/files/h/1",   "0",             4);
  generateFile("tmp/files/i/1/2", "x",             20);
  generateFile("tmp/files/i/2",   "%",             10);
  generateFile("tmp/files/i/3/1", "insert text",   1);
  generateFile("tmp/files/j/1",   "void",          1);

  /* Initiate the backup. */
  Metadata *metadata = metadataNew();
  initiateBackup(metadata, change_from_mirror_init);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, false);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 26);
  assert_true(metadata->backup_history_length == 0);
  assert_true(metadata->total_path_count == cwd_depth() + 26);

  /* Populate stat cache. */
  PathNode *files = findFilesNode(metadata, BH_added, 10);

  PathNode *b = findSubnode(files, "b", BH_added, BPOL_mirror, 1, 2);
  cachedStat(b->path, sStat);
  cachedStat(findSubnode(b, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);
  cachedStat(findSubnode(b, "2", BH_added, BPOL_track, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "d", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *e = findSubnode(files, "e", BH_added, BPOL_mirror, 1, 1);
  cachedStat(e->path, sStat);
  cachedStat(findSubnode(e, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  cachedStat(findSubnode(files, "g", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  PathNode *i = findSubnode(files, "i", BH_added, BPOL_mirror, 1, 3);
  cachedStat(i->path, sStat);
  PathNode *i_1 = findSubnode(i, "1", BH_added, BPOL_copy, 1, 1);
  cachedStat(i_1->path, sStat);
  cachedStat(findSubnode(i_1, "2", BH_added, BPOL_track, 1, 0)->path, sStat);
  cachedStat(findSubnode(i, "2", BH_added, BPOL_mirror, 1, 0)->path, sStat);
  PathNode *i_3 = findSubnode(i, "3", BH_added, BPOL_track, 1, 1);
  cachedStat(i_3->path, sStat);
  cachedStat(findSubnode(i_3, "1", BH_added, BPOL_track, 1, 0)->path, sStat);

  PathNode *j = findSubnode(files, "j", BH_added, BPOL_mirror, 1, 1);
  cachedStat(j->path, sStat);
  cachedStat(findSubnode(j, "1", BH_added, BPOL_mirror, 1, 0)->path, sStat);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

/** Mirror counterpart to policyChangeFromNoneChange(). */
static void policyChangeFromMirrorChange(SearchNode *change_from_mirror_final)
{
  /* Remove various files directly before the initiation. */
  removePath("tmp/files/b/2");
  removePath("tmp/files/b/1");
  removePath("tmp/files/b");
  removePath("tmp/files/d");
  removePath("tmp/files/e/1");
  removePath("tmp/files/e");
  removePath("tmp/files/g");
  removePath("tmp/files/i/1/2");
  removePath("tmp/files/i/1");
  removePath("tmp/files/i/2");
  removePath("tmp/files/i/3/1");
  removePath("tmp/files/i/3");
  removePath("tmp/files/i");
  removePath("tmp/files/j/1");
  removePath("tmp/files/j");

  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 26);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 26);
  initiateBackup(metadata, change_from_mirror_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 7);
  assert_true(metadata->backup_history_length == 1);
  assert_true(metadata->total_path_count == cwd_depth() + 23);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 21);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 10);

  PathNode *a = findSubnode(files, "a", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(a, &metadata->current_backup);
  PathNode *a_1 = findSubnode(a, "1", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 1);
  mustHaveDirectoryStat(a_1, &metadata->current_backup);
  PathNode *a_2 = findSubnode(a_1, "2", BH_not_part_of_repository | BH_policy_changed, BPOL_none, 1, 0);
  mustHaveRegularStat(a_2, &metadata->current_backup, 0, NULL, 0);

  PathNode *b = findSubnode(files, "b", BH_removed | BH_policy_changed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(b, &metadata->backup_history[0]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[0], 9, (uint8_t *)"random123", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2, &metadata->backup_history[0], 10, (uint8_t *)"Foo-Barbar", 0);

  PathNode *c = findSubnode(files, "c", BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[0]);
  PathNode *c_1 = findSubnode(c, "1", BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c_1, &metadata->backup_history[0]);
  PathNode *c_2 = findSubnode(c_1, "2", BH_policy_changed, BPOL_copy, 1, 0);
  mustHaveRegularStat(c_2, &metadata->backup_history[0], 9, (uint8_t *)"987654321", 0);

  PathNode *d = findSubnode(files, "d", BH_removed | BH_policy_changed, BPOL_copy, 1, 0);
  mustHaveRegularCached(d, &metadata->backup_history[0], 9, (uint8_t *)"some text", 0);

  PathNode *e = findSubnode(files, "e", BH_removed | BH_policy_changed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(e, &metadata->backup_history[0]);
  PathNode *e_1 = findSubnode(e, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(e_1, &metadata->backup_history[0], 13, (uint8_t *)"tmp/files/e/1", 0);

  PathNode *f = findSubnode(files, "f", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(f, &metadata->backup_history[0], 13, (uint8_t *)"... Files_e_1", 0);

  PathNode *g = findSubnode(files, "g", BH_removed | BH_policy_changed, BPOL_track, 2, 0);
  mustHaveNonExisting(g, &metadata->current_backup);
  mustHaveRegularCached(g, &metadata->backup_history[0], 0, NULL, 0);

  PathNode *h = findSubnode(files, "h", BH_policy_changed, BPOL_track, 1, 1);
  mustHaveDirectoryStat(h, &metadata->backup_history[0]);
  PathNode *h_1 = findSubnode(h, "1", BH_policy_changed, BPOL_track, 1, 0);
  mustHaveRegularStat(h_1, &metadata->backup_history[0], 4, (uint8_t *)"0000", 0);

  PathNode *i = findSubnode(files, "i", BH_removed | BH_policy_changed, BPOL_track, 2, 3);
  mustHaveNonExisting(i, &metadata->current_backup);
  mustHaveDirectoryCached(i, &metadata->backup_history[0]);
  PathNode *i_1 = findSubnode(i, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(i_1, &metadata->backup_history[0]);
  PathNode *i_1_2 = findSubnode(i_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(i_1_2, &metadata->backup_history[0], 20, (uint8_t *)"xxxxxxxxxxxxxxxxxxxx", 0);
  PathNode *i_2 = findSubnode(i, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(i_2, &metadata->backup_history[0], 10, (uint8_t *)"%%%%%%%%%%", 0);
  PathNode *i_3 = findSubnode(i, "3", BH_removed, BPOL_track, 2, 1);
  mustHaveNonExisting(i_3, &metadata->current_backup);
  mustHaveDirectoryCached(i_3, &metadata->backup_history[0]);
  PathNode *i_3_1 = findSubnode(i_3, "1", BH_removed, BPOL_track, 2, 0);
  mustHaveNonExisting(i_3_1, &metadata->current_backup);
  mustHaveRegularCached(i_3_1, &metadata->backup_history[0], 11, (uint8_t *)"insert text", 0);

  PathNode *j = findSubnode(files, "j", BH_removed | BH_policy_changed, BPOL_track, 2, 1);
  mustHaveNonExisting(j, &metadata->current_backup);
  mustHaveDirectoryCached(j, &metadata->backup_history[0]);
  PathNode *j_1 = findSubnode(j, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(j_1, &metadata->backup_history[0], 4, (uint8_t *)"void", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);

  /* Remove various files to prevent rediscovering. */
  removePath("tmp/files/a/1/2");
  removePath("tmp/files/a/1");
  removePath("tmp/files/a");
}

/** Mirror counterpart to policyChangeFromNonePost(). */
static void policyChangeFromMirrorPost(SearchNode *change_from_mirror_final)
{
  /* Initiate the backup. */
  Metadata *metadata = metadataLoad("tmp/repo/metadata");
  assert_true(metadata->total_path_count == cwd_depth() + 23);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), cwd_depth() + 7);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 21);
  initiateBackup(metadata, change_from_mirror_final);

  /* Check the initiated backup. */
  checkMetadata(metadata, 0, true);
  assert_true(metadata->current_backup.ref_count == cwd_depth() + 2);
  assert_true(metadata->backup_history_length == 2);
  assert_true(metadata->total_path_count == cwd_depth() + 23);
  checkHistPoint(metadata, 0, 0, phase_timestamps(backup_counter() - 1), 5);
  checkHistPoint(metadata, 1, 1, phase_timestamps(backup_counter() - 2), 21);

  PathNode *files = findFilesNode(metadata, BH_unchanged, 9);

  PathNode *b = findSubnode(files, "b", BH_removed, BPOL_none, 1, 2);
  mustHaveDirectoryCached(b, &metadata->backup_history[1]);
  PathNode *b_1 = findSubnode(b, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(b_1, &metadata->backup_history[1], 9, (uint8_t *)"random123", 0);
  PathNode *b_2 = findSubnode(b, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(b_2, &metadata->backup_history[1], 10, (uint8_t *)"Foo-Barbar", 0);

  PathNode *c = findSubnode(files, "c", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c, &metadata->backup_history[1]);
  PathNode *c_1 = findSubnode(c, "1", BH_unchanged, BPOL_copy, 1, 1);
  mustHaveDirectoryStat(c_1, &metadata->backup_history[1]);
  PathNode *c_2 = findSubnode(c_1, "2", BH_unchanged, BPOL_copy, 1, 0);
  mustHaveRegularStat(c_2, &metadata->backup_history[1], 9, (uint8_t *)"987654321", 0);

  PathNode *d = findSubnode(files, "d", BH_removed, BPOL_copy, 1, 0);
  mustHaveRegularCached(d, &metadata->backup_history[1], 9, (uint8_t *)"some text", 0);

  PathNode *e = findSubnode(files, "e", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(e, &metadata->backup_history[1]);
  PathNode *e_1 = findSubnode(e, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(e_1, &metadata->backup_history[1], 13, (uint8_t *)"tmp/files/e/1", 0);

  PathNode *f = findSubnode(files, "f", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(f, &metadata->backup_history[1], 13, (uint8_t *)"... Files_e_1", 0);

  PathNode *g = findSubnode(files, "g", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(g, &metadata->backup_history[0]);
  mustHaveRegularCached(g, &metadata->backup_history[1], 0, NULL, 0);

  PathNode *h = findSubnode(files, "h", BH_unchanged, BPOL_track, 1, 1);
  mustHaveDirectoryStat(h, &metadata->backup_history[1]);
  PathNode *h_1 = findSubnode(h, "1", BH_unchanged, BPOL_track, 1, 0);
  mustHaveRegularStat(h_1, &metadata->backup_history[1], 4, (uint8_t *)"0000", 0);

  PathNode *i = findSubnode(files, "i", BH_unchanged, BPOL_track, 2, 3);
  mustHaveNonExisting(i, &metadata->backup_history[0]);
  mustHaveDirectoryCached(i, &metadata->backup_history[1]);
  PathNode *i_1 = findSubnode(i, "1", BH_removed, BPOL_copy, 1, 1);
  mustHaveDirectoryCached(i_1, &metadata->backup_history[1]);
  PathNode *i_1_2 = findSubnode(i_1, "2", BH_removed, BPOL_track, 1, 0);
  mustHaveRegularCached(i_1_2, &metadata->backup_history[1], 20, (uint8_t *)"xxxxxxxxxxxxxxxxxxxx", 0);
  PathNode *i_2 = findSubnode(i, "2", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(i_2, &metadata->backup_history[1], 10, (uint8_t *)"%%%%%%%%%%", 0);
  PathNode *i_3 = findSubnode(i, "3", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(i_3, &metadata->backup_history[0]);
  mustHaveDirectoryCached(i_3, &metadata->backup_history[1]);
  PathNode *i_3_1 = findSubnode(i_3, "1", BH_unchanged, BPOL_track, 2, 0);
  mustHaveNonExisting(i_3_1, &metadata->backup_history[0]);
  mustHaveRegularCached(i_3_1, &metadata->backup_history[1], 11, (uint8_t *)"insert text", 0);

  PathNode *j = findSubnode(files, "j", BH_unchanged, BPOL_track, 2, 1);
  mustHaveNonExisting(j, &metadata->backup_history[0]);
  mustHaveDirectoryCached(j, &metadata->backup_history[1]);
  PathNode *j_1 = findSubnode(j, "1", BH_removed, BPOL_mirror, 1, 0);
  mustHaveRegularCached(j_1, &metadata->backup_history[1], 4, (uint8_t *)"void", 0);

  /* Finish the backup and perform additional checks. */
  completeBackup(metadata);
  assert_true(countItemsInDir("tmp/repo") == 1);
}

int main(void)
{
  initBackupCommon();

  testGroupStart("policy change from none");
  SearchNode *change_from_none_init  = searchTreeLoad("generated-config-files/policy-change-from-none-init.txt");
  SearchNode *change_from_none_final = searchTreeLoad("generated-config-files/policy-change-from-none-final.txt");

  policyChangeFromNoneInit(change_from_none_init);
  policyChangeFromNoneChange(change_from_none_final);
  policyChangeFromNonePost(change_from_none_final);
  testGroupEnd();

  testGroupStart("policy change from copy");
  SearchNode *change_from_copy_init  = searchTreeLoad("generated-config-files/policy-change-from-copy-init.txt");
  SearchNode *change_from_copy_final = searchTreeLoad("generated-config-files/policy-change-from-copy-final.txt");

  policyChangeFromCopyInit(change_from_copy_init);
  policyChangeFromCopyChange(change_from_copy_final);
  policyChangeFromCopyPost(change_from_copy_final);
  testGroupEnd();

  testGroupStart("policy change from mirror");
  SearchNode *change_from_mirror_init  = searchTreeLoad("generated-config-files/policy-change-from-mirror-init.txt");
  SearchNode *change_from_mirror_final = searchTreeLoad("generated-config-files/policy-change-from-mirror-final.txt");

  policyChangeFromMirrorInit(change_from_mirror_init);
  policyChangeFromMirrorChange(change_from_mirror_final);
  policyChangeFromMirrorPost(change_from_mirror_final);
  testGroupEnd();
}
