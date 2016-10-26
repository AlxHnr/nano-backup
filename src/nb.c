/*
  Copyright (c) 2016 Alexander Heinrich

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
  Implements the nb program.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backup.h"
#include "colors.h"
#include "restore.h"
#include "metadata.h"
#include "regex-pool.h"
#include "search-tree.h"
#include "informations.h"
#include "string-utils.h"
#include "safe-wrappers.h"
#include "error-handling.h"
#include "garbage-collector.h"

/** Prompts the user to proceed.

  @param question The message which will be shown to the user.

  @return True if the user entered "y" or "yes". False if "n" or "no" was
  entered. False will also be returned if stdin has reached its end.
*/
static bool askYesNo(const char *question)
{
  char *line = NULL;
  while(true)
  {
    printf("%s (y/n) ", question);

    free(line);
    line = sReadLine(stdin);

    if(line == NULL)
    {
      return false;
    }
    else if(strcmp(line, "y") == 0 || strcmp(line, "yes") == 0)
    {
      free(line);
      return true;
    }
    else if(strcmp(line, "n") == 0 || strcmp(line, "no") == 0)
    {
      free(line);
      return false;
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
static void printStats(const char *summary, TextColor color,
                       ChangeStats stats, bool *printed_stats)
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
  colorPrintf(stdout, color, "%zu", stats.count);
  printf(" (");
  printHumanReadableSize(stats.size);
  printf(")");
}

/** Runs collectGarbage() and prints the returned statistics.

  @param metadata The metadata to pass to collectGarbage().
  @param repo_path The path to the repository to pass to collectGarbage().
  @param prepend_newline True if a newline should be printed first.
*/
static void runGC(Metadata *metadata, const char *repo_path,
                  bool prepend_newline)
{
  GCStats gc_stats = collectGarbage(metadata, repo_path);

  if(gc_stats.count > 0)
  {
    printf("%sDiscarded unreferenced item%s: ",
           prepend_newline? "\n":"",
           gc_stats.count == 1? "":"s");
    colorPrintf(stdout, TC_blue_bold, "%zu", gc_stats.count);
    printf(" (");
    printHumanReadableSize(gc_stats.size);
    printf(")\n");
  }
}

/** Returns true if the given struct contains any changes. */
static bool containsChanges(MetadataChanges changes)
{
  return
    changes.new_items.count > 0 ||
    changes.removed_items.count > 0 ||
    changes.wiped_items.count > 0 ||
    changes.changed_items.count > 0 ||
    changes.other == true;
}

/** The default action when only the repository is given.

  @param repo_arg The repository path argument as provided by the user.
*/
static void backup(const char *repo_arg)
{
  String repo_path = strRemoveTrailingSlashes(str(repo_arg));
  String config_path = strAppendPath(repo_path, str("config"));
  String metadata_path = strAppendPath(repo_path, str("metadata"));
  String tmp_file_path = strAppendPath(repo_path, str("tmp-file"));

  if(!sPathExists(config_path.str))
  {
    die("repository has no config file: \"%s\"", repo_arg);
  }
  SearchNode *root_node = searchTreeLoad(config_path.str);

  Metadata *metadata =
    sPathExists(metadata_path.str)?
    metadataLoad(metadata_path.str):
    metadataNew();

  initiateBackup(metadata, root_node);
  MetadataChanges changes = printMetadataChanges(metadata);
  printSearchTreeInfos(root_node);

  if(containsChanges(changes))
  {
    printf("\n");

    bool printed_stats = false;
    if(changes.new_items.count > 0)
    {
      printStats("New", TC_green_bold, changes.new_items,
                 &printed_stats);
    }
    if(changes.removed_items.count > 0)
    {
      printStats("Removed", TC_red_bold, changes.removed_items,
                 &printed_stats);
    }
    if(changes.wiped_items.count > 0)
    {
      printStats("Lost", TC_blue_bold, changes.wiped_items,
                 &printed_stats);
    }
    if(printed_stats)
    {
      printf("\n\n");
    }

    if(askYesNo("proceed?"))
    {
      finishBackup(metadata, repo_arg, tmp_file_path.str);
      metadataWrite(metadata, repo_arg, tmp_file_path.str,
                    metadata_path.str);

      runGC(metadata, repo_arg, true);
    }
  }
}

/** Loads the metadata of the specified repository. */
static Metadata *metadataLoadFromRepo(const char *repo_arg)
{
  String repo_path = strRemoveTrailingSlashes(str(repo_arg));
  String metadata_path = strAppendPath(repo_path, str("metadata"));

  if(!sPathExists(metadata_path.str))
  {
    die("repository has no metadata: \"%s\"", repo_arg);
  }

  return metadataLoad(metadata_path.str);
}

/** Ensures that the given path is absolute. */
static const char *buildFullPath(const char *path)
{
  if(path[0] == '/')
  {
    return path;
  }
  else
  {
    char *cwd = sGetCwd();
    String full_path = strAppendPath(str(cwd), str(path));
    free(cwd);

    return full_path.str;
  }
}

/** Restores a path.

  @param repo_arg The repository path as specified by the user.
  @param id The id to which the path should be restored.
  @param path The path to restore.
*/
static void restore(const char *repo_arg, size_t id, const char *path)
{
  Metadata *metadata = metadataLoadFromRepo(repo_arg);
  initiateRestore(metadata, id, buildFullPath(path));

  if(containsChanges(printMetadataChanges(metadata)) &&
     printf("\n") == 1 && askYesNo("restore?"))
  {
    finishRestore(metadata, id, repo_arg);
  }
}

int main(const int arg_count, const char **arg_list)
{
  if(arg_count < 2)
  {
    die("no repository specified");
  }
  else if(!sPathExists(arg_list[1]))
  {
    die("repository doesn't exist: \"%s\"", arg_list[1]);
  }
  else if(!S_ISDIR(sStat(arg_list[1]).st_mode))
  {
    die("not a directory: \"%s\"", arg_list[1]);
  }

  if(arg_count == 2)
  {
    backup(arg_list[1]);
  }
  else if(strcmp(arg_list[2], "gc") == 0)
  {
    if(arg_count > 3)
    {
      die("too many arguments for gc command");
    }

    runGC(metadataLoadFromRepo(arg_list[1]), arg_list[1], false);
  }
  else if(regexec(rpCompile("^[0-9]+$", __FILE__, __LINE__),
                  arg_list[2], 0, NULL, 0) == 0)
  {
    if(arg_count > 4)
    {
      die("too many paths to restore");
    }

    restore(arg_list[1], sStringToSize(arg_list[2]),
            arg_count == 4? arg_list[3]:"/");
  }
  else
  {
    die("invalid arguments");
  }
}
