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

/* Helper strings for parsing the config file. */
static String copy   = { .str = "[copy]",   .length = 6 };
static String mirror = { .str = "[mirror]", .length = 8 };
static String track  = { .str = "[track]",  .length = 7 };

/** Returns a string slice, containing the current line in the given config
  files data.

  @param config The content of an entire config file.
  @param start The position of the line beginning in content.

  @return A string slice, which points inside the given config files data.
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
  FileContent config = sGetFilesContent(path);
  StringMatcherList **common_exclude_matcher_list =
    mpAlloc(sizeof *common_exclude_matcher_list);

  /* Initialize the root node of this tree. */
  SearchNode *root_node = mpAlloc(sizeof *root_node);
  root_node->matcher = NULL;
  root_node->policy = BPOL_none;
  root_node->policy_inherited = false;
  root_node->subnodes = NULL;
  root_node->subnodes_contain_regex = false;
  root_node->exclude_matcher_list = common_exclude_matcher_list;
  root_node->next = NULL;

  size_t parser_position = 0;
  while(parser_position < config.size)
  {
    String line = getLine(config, parser_position);
    parser_position += line.length;
  }

  free(config.content);
  return root_node;
}
