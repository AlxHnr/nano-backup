#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backup.h"
#include "colors.h"
#include "error-handling.h"
#include "garbage-collector.h"
#include "informations.h"
#include "integrity.h"
#include "metadata.h"
#include "restore.h"
#include "safe-math.h"
#include "safe-wrappers.h"
#include "search-tree.h"
#include "str.h"

static void ensureUserConsent(const char *question,
                              Allocator *reusable_buffer)
{
  while(true)
  {
    printf("%s (y/n) ", question);
    if(!sIsTTY(stdin))
    {
      printf("\n");
    }

    StringView line = str("");
    if(!sReadLine(stdin, reusable_buffer, &line) ||
       strIsEqual(line, str("n")) || strIsEqual(line, str("no")))
    {
      exit(EXIT_FAILURE);
    }
    if(strIsEqual(line, str("y")) || strIsEqual(line, str("yes")))
    {
      break;
    }
  }
}

/** Prints the given statistics.

  @param summary A short summary of the change type.
  @param color The color to print the count of affected files by this
  change.
  @param stats The statistics to print.
  @param printed_stats Stores whether this function was already called or
  not. Will be updated by this function.
*/
static void printStats(const char *summary, const TextColor color,
                       const ChangeDetail stats, bool *printed_stats)
{
  if(*printed_stats)
  {
    printf(", ");
  }
  else
  {
    *printed_stats = true;
  }

  printf("%s: ", summary);
  colorPrintf(stdout, color, "%zu", stats.affected_items_count);
  printf(" (");
  printHumanReadableSize(stats.affected_items_total_size);
  printf(")");
}

static void startOverprintingPreviousLine(void)
{
  if(sIsTTY(stdout))
  {
    printf("\033[1F\033[2K");
  }
}

static bool shouldUpdateProgressLine(uint64_t *last_print_timestamp)
{
  const uint64_t now = sTimeMilliseconds();
  if(sUint64GetDifference(*last_print_timestamp, now) > 50)
  {
    *last_print_timestamp = now;
    return true;
  }
  return false;
}

static void printProgress(const bool assume_is_finished,
                          const uint64_t processed_amount,
                          const uint64_t total_amount,
                          const uint64_t amount_to_format,
                          const char *info_text,
                          const char *formatted_action_suffix)
{
  startOverprintingPreviousLine();
  printf("%s... ", info_text);

  if(assume_is_finished)
  {
    colorPrintf(stdout, TC_bold, "100.0%%");
    printf(" (");
    printHumanReadableSize(amount_to_format);
    printf(" %s)\n", formatted_action_suffix);
  }
  else if(processed_amount >= total_amount)
  {
    printf("99.9%% (");
    printHumanReadableSize(amount_to_format);
    printf(" %s)\n", formatted_action_suffix);
  }
  else if(processed_amount > 0)
  {
    const size_t permille =
      sUint64Mul(processed_amount, 1000) / total_amount;
    printf("%3zu.%zu%% (", permille / 10, permille % 10);
    printHumanReadableSize(amount_to_format);
    printf(" %s)\n", formatted_action_suffix);
  }
  else
  {
    printf("\n");
  }
}

static void printGCProgress(const bool assume_is_finished,
                            const size_t items_visited,
                            const size_t max_call_limit,
                            const uint64_t deleted_items_size)
{
  printProgress(assume_is_finished, items_visited, max_call_limit,
                deleted_items_size, "Discarding unreferenced data",
                "deleted");
}

typedef struct
{
  size_t items_visited;
  uint64_t last_print_timestamp;
} GCProgressContext;

static void gcProgressCallback(const uint64_t deleted_items_size,
                               const size_t max_call_limit,
                               void *user_data)
{
  GCProgressContext *ctx = user_data;

  if(shouldUpdateProgressLine(&ctx->last_print_timestamp))
  {
    printGCProgress(false, ctx->items_visited, max_call_limit,
                    deleted_items_size);
  }
  ctx->items_visited++;
}

static void runGC(const Metadata *metadata, StringView repo_path,
                  const bool prepend_newline)
{
  if(prepend_newline)
  {
    printf("\n");
  }

  GCProgressCallback *gc_progress_callback = NULL;
  if(sIsTTY(stdout))
  {
    gc_progress_callback = gcProgressCallback;
    printf("\n");
    printGCProgress(false, 0, 100, 0);
  }

  const GCStatistics gc_stats = collectGarbageProgress(
    metadata, repo_path, gc_progress_callback, &(GCProgressContext){ 0 });
  printGCProgress(true, 0, 100, gc_stats.deleted_items_total_size);
}

static void printIntegrityProgress(const bool assume_is_finished,
                                   const uint64_t bytes_processed,
                                   const uint64_t total_bytes_to_process)
{
  printProgress(assume_is_finished, bytes_processed,
                total_bytes_to_process, bytes_processed,
                "Checking integrity", "processed");
}

typedef struct
{
  uint64_t bytes_processed;
  uint64_t last_print_timestamp;
} IntegrityProgressContext;

static void
integrityProgressCallback(const uint64_t processed_block_size,
                          const uint64_t total_bytes_to_process,
                          void *user_data)
{
  IntegrityProgressContext *ctx = user_data;

  ctx->bytes_processed =
    sUint64Add(ctx->bytes_processed, processed_block_size);
  if(sIsTTY(stdout) &&
     shouldUpdateProgressLine(&ctx->last_print_timestamp))
  {
    printIntegrityProgress(false, ctx->bytes_processed,
                           total_bytes_to_process);
  }
}

static void runIntegrityCheck(const Metadata *metadata,
                              StringView repo_path)
{
  CR_Region *r = CR_RegionNew();
  if(sIsTTY(stdout))
  {
    printf("\n");
    printIntegrityProgress(false, 0, 100);
  }

  IntegrityProgressContext ctx = { 0 };
  const ListOfBrokenPathNodes *broken_nodes = checkIntegrity(
    r, metadata, repo_path, integrityProgressCallback, &ctx);
  printIntegrityProgress(true, ctx.bytes_processed, ctx.bytes_processed);

  printf("Status of repository: ");
  if(broken_nodes == NULL)
  {
    colorPrintf(stdout, TC_green_bold, "Healthy\n");
  }
  else
  {
    colorPrintf(stdout, TC_red_bold, "Incomplete\n\n");
  }

  size_t broken_node_count = 0;
  for(const ListOfBrokenPathNodes *path_node = broken_nodes;
      path_node != NULL; path_node = path_node->next)
  {
    colorPrintf(stdout, TC_red_bold, "?? ");
    colorPrintf(stdout, TC_red, "" PRI_STR " ",
                STR_FMT(path_node->node->path));
    printf("(corrupted)\n");
    broken_node_count++;
  }
  CR_RegionRelease(r);

  if(broken_node_count != 0)
  {
    printf("\n");
    die("found %li item%s with corrupted backup history",
        broken_node_count, broken_node_count == 1 ? "" : "s");
  }
}

static void backup(CR_Region *r, StringView repo_arg)
{
  Allocator *a = allocatorWrapRegion(r);

  StringView repo_path = strStripTrailingSlashes(repo_arg);
  StringView config_path = strAppendPath(repo_path, str("config"), a);
  StringView metadata_path = strAppendPath(repo_path, str("metadata"), a);
  StringView tmp_file_path = strAppendPath(repo_path, str("tmp-file"), a);

  if(!sPathExists(config_path))
  {
    die("repository has no config file: \"" PRI_STR "\"",
        STR_FMT(repo_arg));
  }
  repoLock(r, repo_path, RLH_readwrite);
  SearchNode *root_node = searchTreeLoad(r, config_path);

  Metadata *metadata = sPathExists(metadata_path)
    ? metadataLoad(r, metadata_path)
    : metadataNew(r);

  initiateBackup(metadata, root_node);
  ChangeSummary changes =
    printMetadataChanges(metadata, *root_node->summarize_expressions);
  printSearchTreeInfos(root_node);

  if(containsChanges(&changes))
  {
    printf("\n");

    bool printed_stats = false;
    if(changes.new_items.affected_items_count > 0)
    {
      printStats("New", TC_green_bold, changes.new_items, &printed_stats);
    }
    if(changes.removed_items.affected_items_count > 0)
    {
      printStats("Removed", TC_red_bold, changes.removed_items,
                 &printed_stats);
    }
    if(changes.lost_items.affected_items_count > 0)
    {
      printStats("Lost", TC_blue_bold, changes.lost_items, &printed_stats);
    }
    if(printed_stats)
    {
      printf("\n\n");
    }

    ensureUserConsent("proceed?", allocatorWrapOneSingleGrowableBuffer(r));
    finishBackup(metadata, repo_arg, tmp_file_path);
    metadataWrite(metadata, repo_arg, tmp_file_path, metadata_path);

    runGC(metadata, repo_arg, true);
  }
}

static Metadata *metadataLoadFromRepo(CR_Region *r, StringView repo_arg,
                                      const RepoLockHint lock_hint)
{
  StringView repo_path = strStripTrailingSlashes(repo_arg);
  StringView metadata_path =
    strAppendPath(repo_path, str("metadata"), allocatorWrapRegion(r));

  if(!sPathExists(metadata_path))
  {
    die("repository has no metadata: \"" PRI_STR "\"", STR_FMT(repo_arg));
  }

  repoLock(r, repo_path, lock_hint);
  return metadataLoad(r, metadata_path);
}

static StringView buildFullPath(Allocator *a, StringView path)
{
  if(path.content[0] == '/')
  {
    return path;
  }

  StringView cwd = sGetCurrentDir(a);
  return strAppendPath(strStripTrailingSlashes(cwd), path, a);
}

/** Restores a path.

  @param repo_arg The repository path as specified by the user.
  @param id The id to which the path should be restored.
  @param path The path to restore.
*/
static void restore(CR_Region *r, StringView repo_arg, const size_t id,
                    StringView path)
{
  Metadata *metadata = metadataLoadFromRepo(r, repo_arg, RLH_readonly);
  StringView full_path =
    strStripTrailingSlashes(buildFullPath(allocatorWrapRegion(r), path));
  initiateRestore(metadata, id, full_path);

  const ChangeSummary changes = printMetadataChanges(metadata, NULL);
  if(containsChanges(&changes) && printf("\n") == 1)
  {
    ensureUserConsent("restore?", allocatorWrapOneSingleGrowableBuffer(r));
    finishRestore(metadata, id, repo_arg);
  }
}

int main(const int arg_count, const char **arg_list)
{
  CR_Region *r = CR_RegionNew();
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  if(arg_count < 2)
  {
    die("no repository specified");
  }

  StringView path_to_repo = str(arg_list[1]);
  if(!sPathExists(path_to_repo))
  {
    die("repository doesn't exist: \"%s\"", arg_list[1]);
  }
  else if(!S_ISDIR(sStat(path_to_repo).st_mode))
  {
    die("not a directory: \"%s\"", arg_list[1]);
  }

  if(arg_count == 2)
  {
    backup(r, path_to_repo);
  }
  else if(strcmp(arg_list[2], "gc") == 0)
  {
    if(arg_count > 3)
    {
      die("too many arguments for gc command");
    }

    runGC(metadataLoadFromRepo(r, path_to_repo, RLH_readwrite),
          path_to_repo, false);
  }
  else if(strcmp(arg_list[2], "integrity") == 0)
  {
    if(arg_count > 3)
    {
      die("too many arguments for integrity command");
    }

    runIntegrityCheck(metadataLoadFromRepo(r, path_to_repo, RLH_readonly),
                      path_to_repo);
  }
  else if(sRegexIsMatching(
            sRegexCompile(r, str("^[0-9]+$"), str(__FILE__), __LINE__),
            str(arg_list[2])))
  {
    if(arg_count > 4)
    {
      die("too many paths to restore");
    }

    restore(r, path_to_repo, sStringToSize(str(arg_list[2])),
            arg_count == 4 ? str(arg_list[3]) : str("/"));
  }
  else
  {
    die("invalid arguments");
  }
}
