#include "search-tree.h"

#include <stdlib.h>
#include <string.h>

#include "CRegion/region.h"

#include "error-handling.h"
#include "safe-wrappers.h"
#include "string-table.h"

/** Returns a string slice, containing the current line in the given config
  files data.

  @param config The content of an entire config file.
  @param start The position of the line beginning in content. Must be
  smaller than the size of the config files content.

  @return A string slice, which points into the given config files data.
  Don't modify or free the content of the config file, unless the returned
  string is no longer used.
*/
static StringView getLine(StringView config, const size_t start)
{
  /* Find the index of the line ending. */
  size_t end = start;
  while(end < config.length && config.content[end] != '\n')
    end++;

  return strUnterminated(&config.content[start], end - start);
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
static SearchNode *newNode(CR_Region *r, Allocator *region_wrapper,
                           StringTable *existing_nodes, StringView path,
                           const size_t line_nr)
{
  PathSplit paths = strSplitPath(path);

  /* Ensure that a parent node exists. */
  SearchNode *parent_node = strTableGet(existing_nodes, paths.head);
  if(parent_node == NULL)
  {
    parent_node =
      newNode(r, region_wrapper, existing_nodes, paths.head, line_nr);
  }

  /* Initialize a new node. */
  SearchNode *node = allocate(region_wrapper, sizeof *node);

  /* Build regular expression. */
  if(paths.tail.length >= 2 && paths.tail.content[0] == '/')
  {
    /* Slice out the part after the first slash. */
    StringView expression =
      strUnterminated(&paths.tail.content[1], paths.tail.length - 1);

    strSet(&node->name, strCopy(expression, region_wrapper));
    node->regex = sRegexCompile(r, node->name, str("config"), line_nr);

    parent_node->subnodes_contain_regex = true;
  }
  else
  {
    strSet(&node->name, strCopy(paths.tail, region_wrapper));
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

  strTableMap(existing_nodes, path, node);

  return node;
}

/** Forces all subnodes of the given node to inherit its policy. Subnodes
  that have defined their own policy will be ignored.

  @param parent_node The node that should inherit its policy to its
  subnodes.
*/
static void inheritPolicyToSubnodes(SearchNode *parent_node)
{
  for(SearchNode *node = parent_node->subnodes; node != NULL;
      node = node->next)
  {
    if(node->policy == BPOL_none || node->policy_inherited)
    {
      node->policy = parent_node->policy;
      node->policy_inherited = true;
      node->policy_line_nr = parent_node->policy_line_nr;
      inheritPolicyToSubnodes(node);
    }
  }
}

SearchNode *searchTreeParse(CR_Region *r, StringView config)
{
  if(memchr(config.content, '\0', config.length) != NULL)
  {
    die("config: contains null-bytes");
  }

  Allocator *region_wrapper = allocatorWrapRegion(r);

  /* Initialize the root node of this tree. */
  SearchNode *root_node = allocate(region_wrapper, sizeof *root_node);
  strSet(&root_node->name, str("/"));

  root_node->line_nr = 0;
  root_node->regex = NULL;
  root_node->search_match = SRT_none;
  root_node->policy = BPOL_none;
  root_node->policy_inherited = false;
  root_node->policy_line_nr = 0;
  root_node->subnodes = NULL;
  root_node->subnodes_contain_regex = false;
  root_node->next = NULL;

  /* Initialize expression lists, which are shared across all nodes of the
     tree. */
  root_node->ignore_expressions =
    allocate(region_wrapper, sizeof *root_node->ignore_expressions);
  *root_node->ignore_expressions = NULL;
  root_node->summarize_expressions =
    allocate(region_wrapper, sizeof *root_node->summarize_expressions);
  *root_node->summarize_expressions = NULL;

  /* This table maps paths to existing nodes, without a trailing slash. */
  CR_Region *existing_nodes_region = CR_RegionNew();
  StringTable *existing_nodes = strTableNew(existing_nodes_region);

  /* Associate an empty string with the root node. */
  strTableMap(existing_nodes, str(""), root_node);

  /* Parse the specified config file. */
  size_t line_nr = 1;
  size_t parser_position = 0;
  BackupPolicy current_policy = BPOL_none;

  /* Skip UTF-8 BOM. */
  if(config.length >= 3 && config.content[0] == (char)0xEF &&
     config.content[1] == (char)0xBB && config.content[2] == (char)0xBF)
  {
    parser_position = 3;
  }

  StringView copy_token = str("[copy]");
  StringView mirror_token = str("[mirror]");
  StringView track_token = str("[track]");
  StringView ignore_token = str("[ignore]");
  StringView summarize_token = str("[summarize]");
  while(parser_position < config.length)
  {
    StringView line = getLine(config, parser_position);

    if(strIsWhitespaceOnly(line) || line.content[0] == '#')
    {
      /* Ignore. */
    }
    else if(strIsEqual(line, copy_token))
    {
      current_policy = BPOL_copy;
    }
    else if(strIsEqual(line, mirror_token))
    {
      current_policy = BPOL_mirror;
    }
    else if(strIsEqual(line, track_token))
    {
      current_policy = BPOL_track;
    }
    else if(strIsEqual(line, ignore_token))
    {
      current_policy = BPOL_ignore;
    }
    else if(strIsEqual(line, summarize_token))
    {
      current_policy = BPOL_summarize;
    }
    else if(line.content[0] == '[' && line.content[line.length - 1] == ']')
    {
      StringView policy =
        strUnterminated(&line.content[1], line.length - 2);
      die("config: line %zu: invalid policy: \"" PRI_STR "\"", line_nr,
          STR_FMT(policy));
    }
    else if(current_policy == BPOL_none)
    {
      die("config: line %zu: pattern without policy: \"" PRI_STR "\"",
          line_nr, STR_FMT(line));
    }
    else if(current_policy == BPOL_ignore ||
            current_policy == BPOL_summarize)
    {
      RegexList *expression = allocate(region_wrapper, sizeof *expression);

      strSet(&expression->expression, strCopy(line, region_wrapper));
      expression->line_nr = line_nr;
      expression->regex =
        sRegexCompile(r, expression->expression, str("config"), line_nr);
      expression->has_matched = false;

      RegexList **shared_expression_list = current_policy == BPOL_summarize
        ? root_node->summarize_expressions
        : root_node->ignore_expressions;

      /* Prepend new expression to the shared expression list. */
      expression->next = *shared_expression_list;
      *shared_expression_list = expression;
    }
    else if(line.content[0] == '/')
    {
      if(strPathContainsDotElements(line))
      {
        die("config: line %zu: path contains \".\" or \"..\": \"" PRI_STR
            "\"",
            line_nr, STR_FMT(line));
      }

      StringView path = strStripTrailingSlashes(line);
      SearchNode *previous_definition = strTableGet(existing_nodes, path);

      /* Terminate with an error if the path was already defined. */
      if(previous_definition != NULL &&
         previous_definition->policy != BPOL_none &&
         !previous_definition->policy_inherited)
      {
        die("config: line %zu: redefining %sline %zu: \"" PRI_STR "\"",
            line_nr,
            previous_definition->policy != current_policy ? "policy of "
                                                          : "",
            previous_definition->policy_line_nr, STR_FMT(line));
      }

      /* Use either the existing node or create a new one. */
      SearchNode *node = previous_definition != NULL
        ? previous_definition
        : newNode(r, region_wrapper, existing_nodes, path, line_nr);

      node->policy = current_policy;
      node->policy_inherited = false;
      node->policy_line_nr = line_nr;
      inheritPolicyToSubnodes(node);
    }
    else
    {
      die("config: line %zu: invalid path: \"" PRI_STR "\"", line_nr,
          STR_FMT(line));
    }

    parser_position += line.length;
    parser_position += parser_position < config.length;
    line_nr++;
  }

  CR_RegionRelease(existing_nodes_region);

  return root_node;
}

SearchNode *searchTreeLoad(CR_Region *r, StringView path_to_config)
{
  CR_Region *disposable_r = CR_RegionNew();
  const FileContent content =
    sGetFilesContent(disposable_r, path_to_config);
  StringView config = strUnterminated(content.content, content.size);

  SearchNode *root_node = searchTreeParse(r, config);
  CR_RegionRelease(disposable_r);

  return root_node;
}
