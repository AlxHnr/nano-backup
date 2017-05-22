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
  Implements a struct that can be used for searching the file system.
*/

#include "search-tree.h"

#include <stdlib.h>
#include <string.h>

#include "regex-pool.h"
#include "memory-pool.h"
#include "string-table.h"
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
  @param start The position of the line beginning in content. Must be
  smaller than the size of the config files content.

  @return A string slice, which points into the given config files data.
  Don't modify or free the content of the config file, unless the returned
  string is no longer used.
*/
static String getLine(String config, size_t start)
{
  /* Find the index of the line ending. */
  size_t end = start;
  while(end < config.length && config.str[end] != '\n') end++;

  return (String){ .str = &config.str[start], .length = end - start };
}

/** Creates a new node and adds it to its parent node. This function does
  not check whether the node is already a subnode of its parent. All parent
  nodes will be created if they do not exist. The only node, which must
  exist in advance is the root node.

  @param existing_nodes A StringTable containing all existing nodes in the
  entire tree. The root node must be mapped to an empty string like "".
  @param path The full filepath of the node that should be created. It
  should not be empty or end with a slash, otherwise it will lead to
  undefined behaviour.
  @param line_nr The number of the line in the config file, on which the
  given path was defined.

  @return A new search node, which has inherited properties from its parent
  node.
*/
static SearchNode *newNode(StringTable *existing_nodes,
                           String path, size_t line_nr)
{
  StringSplit paths = strSplitPath(path);

  /* Ensure that a parent node exists. */
  SearchNode *parent_node = strtableGet(existing_nodes, paths.head);
  if(parent_node == NULL)
  {
    parent_node = newNode(existing_nodes, paths.head, line_nr);
  }

  /* Initialize a new node. */
  SearchNode *node = mpAlloc(sizeof *node);

  /* Build regular expression. */
  if(paths.tail.length >= 2 && paths.tail.str[0] == '/')
  {
    /* Slice out the part after the first slash. */
    String expression =
      (String)
      { .str = &paths.tail.str[1], .length = paths.tail.length - 1 };

    String copy = strCopy(expression);
    memcpy(&node->name, &copy, sizeof(node->name));

    node->regex = rpCompile(node->name.str, "config", line_nr);

    parent_node->subnodes_contain_regex = true;
  }
  else
  {
    String copy = strCopy(paths.tail);
    memcpy(&node->name, &copy, sizeof(node->name));

    node->regex = NULL;
  }

  node->line_nr = line_nr;
  node->search_match = SRT_none;

  /* Inherit policy from parent node. */
  node->policy = parent_node->policy;
  node->policy_inherited = parent_node->policy != BPOL_none;
  node->policy_line_nr = line_nr;

  node->subnodes = NULL;
  node->subnodes_contain_regex = false;
  node->ignore_expressions = parent_node->ignore_expressions;

  /* Prepend node to the parents subnodes. */
  node->next = parent_node->subnodes;
  parent_node->subnodes = node;

  strtableMap(existing_nodes, path, node);

  return node;
}

/** Forces all subnodes of the given node to inherit its policy. Subnodes
  that have defined their own policy will be ignored.

  @param parent_node The node that should inherit its policy to its
  subnodes.
*/
static void inheritPolicyToSubnodes(SearchNode *parent_node)
{
  for(SearchNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
  {
    if(node->policy == BPOL_none || node->policy_inherited == true)
    {
      node->policy = parent_node->policy;
      node->policy_inherited = true;
      node->policy_line_nr = parent_node->policy_line_nr;
      inheritPolicyToSubnodes(node);
    }
  }
}

/** Parses the given config source into a search tree.

  @param config The source of the config file.

  @return The root node of the search tree. All nodes are allocated inside
  the internal memory pool and should not be freed by the caller.
*/
SearchNode *searchTreeParse(String config)
{
  if(memchr(config.str, '\0', config.length) != NULL)
  {
    die("config: contains null-bytes");
  }

  /* Initialize the root node of this tree. */
  SearchNode *root_node = mpAlloc(sizeof *root_node);

  String copy = str("/");
  memcpy(&root_node->name, &copy, sizeof(root_node->name));

  root_node->line_nr = 0;
  root_node->regex = NULL;
  root_node->search_match = SRT_none;
  root_node->policy = BPOL_none;
  root_node->policy_inherited = false;
  root_node->policy_line_nr = 0;
  root_node->subnodes = NULL;
  root_node->subnodes_contain_regex = false;
  root_node->next = NULL;

  /* Initialize ignore expression list, which is shared across all nodes of
     the tree. */
  root_node->ignore_expressions =
    mpAlloc(sizeof *root_node->ignore_expressions);
  *root_node->ignore_expressions = NULL;

  /* This table maps paths to existing nodes, without a trailing slash. */
  StringTable *existing_nodes = strtableNew();

  /* Associate an empty string with the root node. */
  strtableMap(existing_nodes, str(""), root_node);

  /* Parse the specified config file. */
  size_t line_nr = 1;
  size_t parser_position = 0;
  BackupPolicy current_policy = BPOL_none;

  /* Skip UTF-8 BOM. */
  if(config.length >= 3 &&
     config.str[0] == (char)0xEF &&
     config.str[1] == (char)0xBB &&
     config.str[2] == (char)0xBF)
  {
    parser_position = 3;
  }

  while(parser_position < config.length)
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
      /* Slice out and copy the invalid policy name. */
      String policy =
        strCopy((String){ .str = &line.str[1], .length = line.length - 2});
      strtableFree(existing_nodes);

      die("config: line %zu: invalid policy: \"%s\"", line_nr, policy.str);
    }
    else if(current_policy == BPOL_none)
    {
      String pattern = strCopy(line);
      strtableFree(existing_nodes);

      die("config: line %zu: pattern without policy: \"%s\"",
          line_nr, pattern.str);
    }
    else if(current_policy == BPOL_ignore)
    {
      /* Initialize new ignore expression. */
      RegexList *ignore_expression = mpAlloc(sizeof *ignore_expression);

      String copy = strCopy(line);
      memcpy(&ignore_expression->expression, &copy,
             sizeof(ignore_expression->expression));

      ignore_expression->line_nr = line_nr;

      /* Pass the copy of the current line, to ensure that the string is
         null-terminated. */
      ignore_expression->regex =
        rpCompile(ignore_expression->expression.str, "config", line_nr);

      ignore_expression->has_matched = false;

      /* Prepend new expression to the shared ignore expression list. */
      ignore_expression->next = *root_node->ignore_expressions;
      *root_node->ignore_expressions = ignore_expression;
    }
    else if(line.str[0] == '/')
    {
      if(strPathContainsDotElements(line))
      {
        strtableFree(existing_nodes);

        die("config: line %zu: path contains \".\" or \"..\": \"%s\"",
            line_nr, strCopy(line).str);
      }

      String path = strRemoveTrailingSlashes(line);
      SearchNode *previous_definition = strtableGet(existing_nodes, path);

      /* Terminate with an error if the path was already defined. */
      if(previous_definition != NULL &&
         previous_definition->policy != BPOL_none &&
         previous_definition->policy_inherited == false)
      {
        String redefined_path = strCopy(line);
        strtableFree(existing_nodes);

        die("config: line %zu: redefining %sline %zu: \"%s\"",
            line_nr, previous_definition->policy != current_policy ?
            "policy of " : "", previous_definition->policy_line_nr,
            redefined_path.str);
      }

      /* Use either the existing node or create a new one. */
      SearchNode *node =
        previous_definition != NULL ?
        previous_definition : newNode(existing_nodes, path, line_nr);

      node->policy = current_policy;
      node->policy_inherited = false;
      node->policy_line_nr = line_nr;
      inheritPolicyToSubnodes(node);
    }
    else
    {
      String path = strCopy(line);
      strtableFree(existing_nodes);

      die("config: line %zu: invalid path: \"%s\"", line_nr, path.str);
    }

    parser_position += line.length;
    parser_position += parser_position < config.length;
    line_nr++;
  }

  strtableFree(existing_nodes);

  return root_node;
}

/** Loads a search tree from the specified config file.

  @param path The path to the config file.

  @return The root node of the search tree. All nodes are allocated inside
  the internal memory pool and should not be freed by the caller.
*/
SearchNode *searchTreeLoad(const char *path)
{
  FileContent content = sGetFilesContent(path);
  String config = { .str = content.content, .length = content.size };

  SearchNode *root_node = searchTreeParse(config);
  free(content.content);

  return root_node;
}
