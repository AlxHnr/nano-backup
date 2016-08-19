/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

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
#include "metadata.h"
#include "search-tree.h"
#include "informations.h"
#include "string-utils.h"
#include "safe-wrappers.h"
#include "error-handling.h"
#include "garbage-collector.h"

/** Prompts the user to proceed.

  @return True if the user entered "y" or "yes". False if "n" or "no" was
  entered. False will also be returned if stdin has reached its end.
*/
static bool askUserProceed(void)
{
  char *line = NULL;
  while(true)
  {
    printf("proceed? (y/n) ");

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

  if(changes.new_items.count   > 0 || changes.removed_items.count > 0 ||
     changes.wiped_items.count > 0 || changes.changed_items.count > 0 ||
     changes.other == true)
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

    if(askUserProceed())
    {
      finishBackup(metadata, repo_arg, tmp_file_path.str);
      metadataWrite(metadata, repo_arg, tmp_file_path.str,
                    metadata_path.str);

      runGC(metadata, repo_arg, true);
    }
  }
}

/** The gc command.

  @param repo_arg The repository path argument as provided by the user.
*/
static void gc(const char *repo_arg)
{
  String repo_path = strRemoveTrailingSlashes(str(repo_arg));
  String metadata_path = strAppendPath(repo_path, str("metadata"));

  runGC(metadataLoad(metadata_path.str), repo_arg, false);
}

int main(const int arg_count, const char **arg_list)
{
  if(arg_count <= 1)
  {
    die("no arguments");
  }
  else if(arg_count > 3)
  {
    die("too many arguments");
  }
  else if(!sPathExists(arg_list[1]))
  {
    die("repository doesn't exist: \"%s\"", arg_list[1]);
  }

  struct stat repo_stats = sStat(arg_list[1]);
  if(!S_ISDIR(repo_stats.st_mode))
  {
    die("not a directory: \"%s\"", arg_list[1]);
  }

  if(arg_count == 2)
  {
    backup(arg_list[1]);
  }
  else if(strcmp(arg_list[2], "gc") == 0)
  {
    gc(arg_list[1]);
  }
  else
  {
    die("invalid argument: \"%s\"", arg_list[2]);
  }
}
