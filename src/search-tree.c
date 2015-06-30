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

/**
  @file search-tree.c Implements a struct that can be used for searching
  the file system.
*/

#include "search-tree.h"

#include <stdlib.h>

#include "memory-pool.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/* Helper strings for parsing the config file. */
static String copy_token   = { .str = "[copy]",   .length = 6 };
static String mirror_token = { .str = "[mirror]", .length = 8 };
static String track_token  = { .str = "[track]",  .length = 7 };
static String ignore_token = { .str = "[ignore]", .length = 8 };

/** Returns a string slice, containing the current line in the given config
  files data.

  @param config The content of an entire config file.
  @param start The position of the line beginning in content.

  @return A string slice, which points into the given config files data.
  Don't modify or free the content of the config file, unless the returned
  string is no longer used.
*/
static String getLine(FileContent config, size_t start)
{
  /* Find the index of the line ending. */
  size_t end = start;
  while(end < config.size && config.content[end] != '\n') end++;

  return (String){ .str = &config.content[start], .length = end - start};
}

/** Loads a search tree from the specified config file.

  @param path The path to the config file.

  @return The root node of the search tree. All nodes are allocated inside
  the internal memory pool and should not be freed by the caller.
*/
SearchNode *searchTreeLoad(const char *path)
{
  /* Initialize the root node of this tree. */
  SearchNode *root_node = mpAlloc(sizeof *root_node);
  root_node->matcher = NULL;
  root_node->policy = BPOL_none;
  root_node->policy_inherited = false;
  root_node->subnodes = NULL;
  root_node->subnodes_contain_regex = false;
  root_node->next = NULL;

  /* Initialized ignore matcher list, which is shared across all nodes of
     the tree. */
  root_node->ignore_matcher_list =
    mpAlloc(sizeof *root_node->ignore_matcher_list);
  *root_node->ignore_matcher_list = NULL;

  /* Parse the specified config file. */
  size_t line_nr = 1;
  size_t parser_position = 0;
  BackupPolicy current_policy = BPOL_none;
  FileContent config = sGetFilesContent(path);

  /* Skip UTF-8 BOM. */
  if(config.size >= 3 &&
     config.content[0] == (char)0xEF &&
     config.content[1] == (char)0xBB &&
     config.content[2] == (char)0xBF)
  {
    parser_position = 3;
  }

  while(parser_position < config.size)
  {
    String line = getLine(config, parser_position);

    if(strWhitespaceOnly(line))
    {
      /* Ignore. */
    }
    else if(strCompare(line, copy_token))
    {
      current_policy = BPOL_copy;
    }
    else if(strCompare(line, mirror_token))
    {
      current_policy = BPOL_mirror;
    }
    else if(strCompare(line, track_token))
    {
      current_policy = BPOL_track;
    }
    else if(strCompare(line, ignore_token))
    {
      current_policy = BPOL_ignore;
    }
    else if(line.str[0] == '[' && line.str[line.length - 1] == ']')
    {
      /* Slice out and copy the unknown policy name. */
      String policy =
        strCopy((String){ .str = &line.str[1], .length = line.length - 2});
      free(config.content);

      die("config: line %zu: unknown policy: \"%s\"", line_nr, policy.str);
    }
    else if(current_policy == BPOL_none)
    {
      String pattern = strCopy(line);
      free(config.content);

      die("config: line %zu: pattern without policy: \"%s\"",
          line_nr, pattern.str);
    }
    else if(current_policy == BPOL_ignore)
    {
      /* Initialize new ignore matcher. */
      StringMatcherList *ignore_matcher = mpAlloc(sizeof *ignore_matcher);
      ignore_matcher->matcher = strmatchRegex(strCopy(line), line_nr);
      ignore_matcher->next = *root_node->ignore_matcher_list;

      /* Prepend new matcher to the shared ignore matcher list. */
      *root_node->ignore_matcher_list = ignore_matcher;
    }
    else if(line.str[0] == '/')
    {
    }
    else
    {
      String path = strCopy(line);
      free(config.content);

      die("config: line %zu: invalid path: \"%s\"", line_nr, path.str);
    }

    parser_position += line.length;
    parser_position += config.content[parser_position] == '\n';
    line_nr++;
  }

  free(config.content);
  return root_node;
}
