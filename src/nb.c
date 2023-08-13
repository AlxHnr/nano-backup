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
#include "regex-pool.h"
#include "restore.h"
#include "safe-wrappers.h"
#include "search-tree.h"
#include "str.h"

static void ensureUserConsent(const char *question)
{
  char *line = NULL;
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

    free(line);
    line = sReadLine(stdin);

    if(line == NULL || strcmp(line, "n") == 0 || strcmp(line, "no") == 0)
    {
      free(line);
      exit(EXIT_FAILURE);
    }
    else if(strcmp(line, "y") == 0 || strcmp(line, "yes") == 0)
    {
      free(line);
      return;
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

static void runGC(const Metadata *metadata, String repo_path,
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

static void runIntegrityCheck(const Metadata *metadata, String repo_path)
{
  CR_Region *r = CR_RegionNew();
  const ListOfBrokenPathNodes *broken_nodes =
    checkIntegrity(r, metadata, repo_path);

  size_t broken_node_count = 0;
  for(const ListOfBrokenPathNodes *path_node = broken_nodes;
      path_node != NULL; path_node = path_node->next)
  {
    colorPrintf(stdout, TC_red_bold, "?? ");
    colorPrintf(stdout, TC_red, "%s ", path_node->node->path.content);
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

static void backup(String repo_arg)
{
  String repo_path = strRemoveTrailingSlashes(repo_arg);
  String config_path = strAppendPath(repo_path, strWrap("config"));
  String metadata_path = strAppendPath(repo_path, strWrap("metadata"));
  String tmp_file_path = strAppendPath(repo_path, strWrap("tmp-file"));

  if(!sPathExists(config_path))
  {
    die("repository has no config file: \"%s\"", repo_arg.content);
  }
  repoLockUntilExit(repo_path);
  SearchNode *root_node = searchTreeLoad(config_path);

  Metadata *metadata = sPathExists(metadata_path)
    ? metadataLoad(metadata_path)
    : metadataNew();

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

    ensureUserConsent("proceed?");
    finishBackup(metadata, repo_arg, tmp_file_path);
    metadataWrite(metadata, repo_arg, tmp_file_path, metadata_path);

    runGC(metadata, repo_arg, true);
  }
}

static Metadata *metadataLoadFromRepo(String repo_arg)
{
  String repo_path = strRemoveTrailingSlashes(repo_arg);
  String metadata_path = strAppendPath(repo_path, strWrap("metadata"));

  if(!sPathExists(metadata_path))
  {
    die("repository has no metadata: \"%s\"", repo_arg.content);
  }

  repoLockUntilExit(repo_path);
  return metadataLoad(metadata_path);
}

static String buildFullPath(String path)
{
  if(path.content[0] == '/')
  {
    return path;
  }
  else
  {
    char *cwd = sGetCwd();
    String full_path =
      strAppendPath(strRemoveTrailingSlashes(strWrap(cwd)), path);
    free(cwd);

    return full_path;
  }
}

/** Restores a path.

  @param repo_arg The repository path as specified by the user.
  @param id The id to which the path should be restored.
  @param path The path to restore.
*/
static void restore(String repo_arg, const size_t id, String path)
{
  Metadata *metadata = metadataLoadFromRepo(repo_arg);
  String full_path = strRemoveTrailingSlashes(buildFullPath(path));
  initiateRestore(metadata, id, strCopy(full_path));

  const ChangeSummary changes = printMetadataChanges(metadata, NULL);
  if(containsChanges(&changes) && printf("\n") == 1)
  {
    ensureUserConsent("restore?");
    finishRestore(metadata, id, repo_arg);
  }
}

int main(const int arg_count, const char **arg_list)
{
  if(arg_count < 2)
  {
    die("no repository specified");
  }

  String path_to_repo = strWrap(arg_list[1]);
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
    backup(path_to_repo);
  }
  else if(strcmp(arg_list[2], "gc") == 0)
  {
    if(arg_count > 3)
    {
      die("too many arguments for gc command");
    }

    runGC(metadataLoadFromRepo(path_to_repo), path_to_repo, false);
  }
  else if(strcmp(arg_list[2], "integrity") == 0)
  {
    if(arg_count > 3)
    {
      die("too many arguments for integrity command");
    }

    runIntegrityCheck(metadataLoadFromRepo(path_to_repo), path_to_repo);
  }
  else if(regexec(
            rpCompile(strWrap("^[0-9]+$"), strWrap(__FILE__), __LINE__),
            arg_list[2], 0, NULL, 0) == 0)
  {
    if(arg_count > 4)
    {
      die("too many paths to restore");
    }

    restore(path_to_repo, sStringToSize(strWrap(arg_list[2])),
            arg_count == 4 ? strWrap(arg_list[3]) : strWrap("/"));
  }
  else
  {
    die("invalid arguments");
  }
}
