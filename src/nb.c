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
    if(fflush(stdout) != 0)
    {
      dieErrno("failed to flush stdout");
    }

    StringView line = sReadLine(stdin, reusable_buffer);
    if(strIsEmpty(line) || strIsEqual(line, str("n")) ||
       strIsEqual(line, str("no")))
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

static void runGC(const Metadata *metadata, StringView repo_path,
                  const bool prepend_newline)
{
  const GCStatistics gc_stats = collectGarbage(metadata, repo_path);

  if(gc_stats.deleted_items_count > 0)
  {
    printf("%sDiscarded unreferenced items: ",
           prepend_newline ? "\n" : "");
    colorPrintf(stdout, TC_blue_bold, "%zu", gc_stats.deleted_items_count);
    printf(" (");
    printHumanReadableSize(gc_stats.deleted_items_total_size);
    printf(")\n");
  }
}

static void runIntegrityCheck(const Metadata *metadata,
                              StringView repo_path)
{
  CR_Region *r = CR_RegionNew();
  const ListOfBrokenPathNodes *broken_nodes =
    checkIntegrity(r, metadata, repo_path);

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
  repoLock(r, repo_path);
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

static Metadata *metadataLoadFromRepo(CR_Region *r, StringView repo_arg)
{
  StringView repo_path = strStripTrailingSlashes(repo_arg);
  StringView metadata_path =
    strAppendPath(repo_path, str("metadata"), allocatorWrapRegion(r));

  if(!sPathExists(metadata_path))
  {
    die("repository has no metadata: \"" PRI_STR "\"", STR_FMT(repo_arg));
  }

  repoLock(r, repo_path);
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
  Metadata *metadata = metadataLoadFromRepo(r, repo_arg);
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

    runGC(metadataLoadFromRepo(r, path_to_repo), path_to_repo, false);
  }
  else if(strcmp(arg_list[2], "integrity") == 0)
  {
    if(arg_count > 3)
    {
      die("too many arguments for integrity command");
    }

    runIntegrityCheck(metadataLoadFromRepo(r, path_to_repo), path_to_repo);
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
