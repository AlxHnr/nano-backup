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
#include "metadata.h"
#include "search-tree.h"
#include "informations.h"
#include "string-utils.h"
#include "safe-wrappers.h"
#include "error-handling.h"

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

int main(const int arg_count, const char **arg_list)
{
  if(arg_count <= 1)
  {
    die("no arguments");
  }
  else if(arg_count > 2)
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

  String repo_path = strRemoveTrailingSlashes(str(arg_list[1]));
  String config_path = strAppendPath(repo_path, str("config"));
  String metadata_path = strAppendPath(repo_path, str("metadata"));
  String tmp_file_path = strAppendPath(repo_path, str("tmp-file"));

  if(!sPathExists(config_path.str))
  {
    die("repository has no config file: \"%s\"", arg_list[1]);
  }
  SearchNode *root_node = searchTreeLoad(config_path.str);

  Metadata *metadata =
    sPathExists(metadata_path.str)?
    metadataLoad(metadata_path.str):
    metadataNew();

  initiateBackup(metadata, root_node);
  MetadataChanges changes = printMetadataChanges(metadata);
  printSearchTreeInfos(root_node);

  printf("\nTotal: +%zu items, +", changes.new_items_count);
  printHumanReadableSize(changes.new_files_size);
  printf("\n\n");

  if(askUserProceed())
  {
    finishBackup(metadata, repo_path.str, tmp_file_path.str);
    metadataWrite(metadata, repo_path.str, tmp_file_path.str,
                  metadata_path.str);
  }
}
