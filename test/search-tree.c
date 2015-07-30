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
  @file search-tree.c Tests loading search trees from config files.
*/

#include "search-tree.h"

#include "test.h"
#include "string-utils.h"
#include "error-handling.h"

/** Returns the node with the given string as its matcher expression. It
  will terminate the test suite with failure, if the node couldn't be
  found.

  @param string The matcher expression string to search for.
  @param starting_node The first node in the list which should be searched.

  @return A valid search node.
*/
static SearchNode *findNode(const char *string, SearchNode *starting_node)
{
  String node_name = str(string);
  for(SearchNode *node = starting_node; node != NULL; node = node->next)
  {
    if(strCompare(node_name, strmatchGetExpression(node->matcher)))
    {
      return node;
    }
  }

  die("failed to find node with name \"%s\"", string);

  /* Some compilers are not aware that die() never returns. */
  return NULL;
}

/** Loads a search tree from a simple config file and checks it.

  @param path A null-terminated string, containing a path to a valid config
  file.
*/
static void testSimpleConfigFile(const char *path)
{
  SearchNode *root_node = searchTreeLoad(path);

  /* Check root node. */
  assert_true(root_node != NULL);
  assert_true(root_node->matcher == NULL);

  assert_true(root_node->policy == BPOL_none);
  assert_true(root_node->policy_inherited == false);
  assert_true(root_node->policy_line_nr == 0);

  assert_true(root_node->subnodes != NULL);
  assert_true(root_node->subnodes->next != NULL);
  assert_true(root_node->subnodes->next->next == NULL);
  assert_true(root_node->subnodes_contain_regex == false);

  assert_true(root_node->ignore_matcher_list != NULL);
  assert_true(*root_node->ignore_matcher_list == NULL);

  assert_true(root_node->next == NULL);

  /* Check etc subnode. */
  SearchNode *etc_node = findNode("etc", root_node->subnodes);

  assert_true(etc_node->matcher != NULL);
  assert_true(strmatchHasMatched(etc_node->matcher) == false);
  assert_true(strmatchLineNr(etc_node->matcher) == 8);
  assert_true(strCompare(strmatchGetExpression(etc_node->matcher),
                         str("etc")));

  assert_true(etc_node->policy == BPOL_track);
  assert_true(etc_node->policy_inherited == false);
  assert_true(etc_node->policy_line_nr == 8);

  assert_true(etc_node->subnodes == NULL);
  assert_true(etc_node->subnodes_contain_regex == false);

  assert_true(etc_node->ignore_matcher_list ==
              root_node->ignore_matcher_list);

  /* Check home subnode. */
  SearchNode *home_node = findNode("home", root_node->subnodes);

  assert_true(home_node->matcher != NULL);
  assert_true(strmatchHasMatched(home_node->matcher) == false);
  assert_true(strmatchLineNr(home_node->matcher) == 2);
  assert_true(strCompare(strmatchGetExpression(home_node->matcher),
                         str("home")));

  assert_true(home_node->policy == BPOL_none);
  assert_true(home_node->policy_inherited == false);
  assert_true(home_node->policy_line_nr == 2);

  assert_true(home_node->subnodes != NULL);
  assert_true(home_node->subnodes->next != NULL);
  assert_true(home_node->subnodes->next->next == NULL);
  assert_true(home_node->subnodes_contain_regex == false);

  assert_true(home_node->ignore_matcher_list ==
              root_node->ignore_matcher_list);
}

int main(void)
{
  testGroupStart("BOM and EOL variations");
  testSimpleConfigFile("config-files/simple.txt");
  testSimpleConfigFile("config-files/simple-BOM.txt");
  testSimpleConfigFile("config-files/simple-noeol.txt");
  testSimpleConfigFile("config-files/simple-BOM-noeol.txt");
  testGroupEnd();

  testGroupStart("broken config files");
  assert_error(searchTreeLoad("non-existing-file.txt"), "failed to access "
               "\"non-existing-file.txt\": No such file or directory");

  assert_error(searchTreeLoad("broken-config-files/invalid-policy.txt"),
               "config: line 7: invalid policy: \"trak\"");

  assert_error(searchTreeLoad("broken-config-files/empty-policy-name.txt"),
               "config: line 9: invalid policy: \"\"");

  assert_error(searchTreeLoad("broken-config-files/opening-brace.txt"),
               "config: line 6: invalid path: \"[foo\"");

  assert_error(searchTreeLoad("broken-config-files/opening-brace-empty.txt"),
               "config: line 9: invalid path: \"[\"");

  assert_error(searchTreeLoad("broken-config-files/closing-brace.txt"),
               "config: line 7: invalid path: \"foo]\"");

  assert_error(searchTreeLoad("broken-config-files/closing-brace-empty.txt"),
               "config: line 3: invalid path: \"]\"");

  assert_error(searchTreeLoad("broken-config-files/invalid-regex.txt"),
               "config: line 5: Unmatched ( or \\(: \"(foo|bar\"");

  assert_error(searchTreeLoad("broken-config-files/pattern-without-policy.txt"),
               "config: line 8: pattern without policy: \"/home/user/foo/bar.txt\"");

  assert_error(searchTreeLoad("broken-config-files/invalid-ignore-expression.txt"),
               "config: line 6: Unmatched [ or [^: \" ([0-9A-Za-z)+///\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-1.txt"),
               "config: line 6: redefining line 4: \"/home/user/foo/Packages/\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-2.txt"),
               "config: line 12: redefining line 6: \"/home/user/foo/Packages\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-3.txt"),
               "config: line 24: redefining line 12: \"/home/\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-root-1.txt"),
               "config: line 11: redefining line 7: \"/\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-root-2.txt"),
               "config: line 17: redefining line 9: \"/\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-policy-1.txt"),
               "config: line 8: redefining policy of line 4: \"/home/user/.config/\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-policy-2.txt"),
               "config: line 21: redefining policy of line 12: \"/home/user/\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-root-policy-1.txt"),
               "config: line 5: redefining policy of line 2: \"/\"");

  assert_error(searchTreeLoad("broken-config-files/redefine-root-policy-2.txt"),
               "config: line 15: redefining policy of line 6: \"/\"");

  assert_error(searchTreeLoad("broken-config-files/invalid-path-1.txt"),
               "config: line 9: invalid path: \"     /foo/bar\"");

  assert_error(searchTreeLoad("broken-config-files/invalid-path-2.txt"),
               "config: line 3: invalid path: \"~/.bashrc\"");

  assert_error(searchTreeLoad("broken-config-files/invalid-path-3.txt"),
               "config: line 7: invalid path: \".bash_history\"");

  assert_error(searchTreeLoad("broken-config-files/multiple-errors.txt"),
               "config: line 9: Invalid preceding regular expression: \"???*\"");

  assert_error(searchTreeLoad("broken-config-files/BOM-simple-error.txt"),
               "config: line 3: invalid path: \"This file contains a UTF-8 BOM.\"");
  testGroupEnd();
}
