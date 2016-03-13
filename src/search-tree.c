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
  Implements a struct that can be used for searching the file system.
*/

#include "search-tree.h"

#include <stdlib.h>
#include <string.h>

#include "memory-pool.h"
#include "string-table.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/* Helper strings for parsing the config file. */
static String copy_token   = { .str = "[copy]",   .length = 6 };
static String mirror_token = { .str = "[mirror]", .length = 8 };
static String track_token  = { .str = "[track]",  .length = 7 };
static String ignore_token = { .str = "[ignore]", .length = 8 };

/** All SearchNodes are allocated inside the internal memory pool and live
 as long as the entire program. Thus regular expressions must be tracked
 separately and freed when the program exits. */
static regex_t **regex_pool = NULL;
static size_t regex_pool_used = 0;
static size_t regex_pool_capacity = 0;

/** Frees the entire regex pool. */
static void freeRegexPool(void)
{
  for(size_t index = 0; index < regex_pool_used; index++)
  {
    regfree(regex_pool[index]);
    free(regex_pool[index]);
  }

  free(regex_pool);
}

/** Compiles the given expression and stores the resulting regex in the
  regex pool. It will terminate the program with an error message if the
  given expression is invalid.

  @param expression A null-terminated string.
  @param line_nr The number of the line in the config file on which the
  given expression was defined. Needed for printing an error message.

  @return A compiled regex which should not be freed by the caller.
*/
static const regex_t *compileRegex(const char *expression, size_t line_nr)
{
  /* Grow regex pool if its used up. */
  if(regex_pool_used == regex_pool_capacity)
  {
    if(regex_pool == NULL) atexit(freeRegexPool);

    size_t new_pool_length =
      regex_pool_capacity == 0 ? 8 : sSizeMul(regex_pool_capacity, 2);
    size_t new_pool_size = sSizeMul(sizeof *regex_pool, new_pool_length);

    regex_pool = sRealloc(regex_pool, new_pool_size);
    regex_pool_capacity = new_pool_length;
  }

  /* The memory for a regex is not allocated inside the internal memory
     pool because it may have a shorter lifetime then the regex pool
     itself. */
  regex_t *regex = sMalloc(sizeof *regex);
  int error = regcomp(regex, expression, REG_EXTENDED | REG_NOSUB);

  if(error != 0)
  {
    size_t error_length = regerror(error, regex, NULL, 0);
    char *error_str = mpAlloc(error_length);
    regerror(error, regex, error_str, error_length);
    free(regex);

    die("config: line %zu: %s: \"%s\"", line_nr, error_str, expression);
  }

  /* Store reference in regex pool. */
  regex_pool[regex_pool_used] = regex;
  regex_pool_used++;

  return regex;
}

/** Returns a string slice, containing the current line in the given config
  files data.

  @param config The content of an entire config file.
  @param start The position of the line beginning in content. Must be
  smaller than the size of the config files content.

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

    node->regex = compileRegex(node->name.str, line_nr);

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

/** Loads a search tree from the specified config file.

  @param path The path to the config file.

  @return The root node of the search tree. All nodes are allocated inside
  the internal memory pool and should not be freed by the caller.
*/
SearchNode *searchTreeLoad(const char *path)
{
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
      /* Slice out and copy the invalid policy name. */
      String policy =
        strCopy((String){ .str = &line.str[1], .length = line.length - 2});
      strtableFree(existing_nodes);
      free(config.content);

      die("config: line %zu: invalid policy: \"%s\"", line_nr, policy.str);
    }
    else if(current_policy == BPOL_none)
    {
      String pattern = strCopy(line);
      strtableFree(existing_nodes);
      free(config.content);

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
        compileRegex(ignore_expression->expression.str, line_nr);

      ignore_expression->has_matched = false;

      /* Prepend new expression to the shared ignore expression list. */
      ignore_expression->next = *root_node->ignore_expressions;
      *root_node->ignore_expressions = ignore_expression;
    }
    else if(line.str[0] == '/')
    {
      String path = strRemoveTrailingSlashes(line);
      SearchNode *previous_definition = strtableGet(existing_nodes, path);

      /* Terminate with an error if the path was already defined. */
      if(previous_definition != NULL &&
         previous_definition->policy != BPOL_none &&
         previous_definition->policy_inherited == false)
      {
        String redefined_path = strCopy(line);
        strtableFree(existing_nodes);
        free(config.content);

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
      free(config.content);

      die("config: line %zu: invalid path: \"%s\"", line_nr, path.str);
    }

    parser_position += line.length;
    parser_position += parser_position < config.size;
    line_nr++;
  }

  strtableFree(existing_nodes);
  free(config.content);

  return root_node;
}
