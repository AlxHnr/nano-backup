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

/** Counts the subnodes of the given node.

  @param parent_node The node containing the subnodes.

  @return The amount of subnodes in the given node.
*/
static size_t countSubnodes(SearchNode *parent_node)
{
  size_t counter = 0;

  for(SearchNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
  {
    counter++;
  }

  return counter;
}

/** Returns the subnode with the given string as its matcher expression. It
  will terminate the test suite with failure if the node does not exist.

  @param parent_node The node containing the subnodes which should be
  searched.
  @param string The matcher expression string to search for.

  @return A valid search node.
*/
static SearchNode *findSubnode(SearchNode *parent_node, const char *string)
{
  String node_name = str(string);
  for(SearchNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
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

/** Counts all ignore matcher that the given SearchNode contains.

  @param node The SearchNode containing the ignore matcher list.

  @return The amount of ignore matcher in the given nodes ignore matcher
  list.
*/
static size_t countIgnoreMatcher(SearchNode *node)
{
  size_t counter = 0;

  for(StringMatcherList *element = *node->ignore_matcher_list;
      element != NULL; element = element->next)
  {
    counter++;
  }

  return counter;
}

/** Checks if a ignore matcher with the given ignore pattern exists in the
  given SearchNode.

  @param node The node containing the ignore matcher list.
  @param pattern The pattern which should be searched for.

  @return True if the pattern exists in the ignore matcher list. False
  otherwise.
*/
static bool ignoreMatcherExists(SearchNode *node, const char *pattern)
{
  for(StringMatcherList *element = *node->ignore_matcher_list;
      element != NULL; element = element->next)
  {
    if(strCompare(strmatchGetExpression(element->matcher), str(pattern)))
    {
      return true;
    }
  }

  return false;
}

/** Checks whether the given node contains the expected values, or not.

  @param node The node which should be checked.
  @param subnode_count The amount of subnodes which the node must have.
  @param subnodes_contain_regex True, if at least one of the subnodes must
  contain a regular expression.
  @param policy The policy which the node must have.
  @param policy_inherited True, if the policy was inherited from the parent
  node.
  @param policy_line_nr The line number on which the policy was defined.
*/
static void checkBasicNode(SearchNode *node, size_t subnode_count,
                           bool subnodes_contain_regex,
                           BackupPolicy policy, bool policy_inherited,
                           size_t policy_line_nr)
{
  assert_true(node != NULL);

  assert_true(node->policy == policy);
  assert_true(node->policy_inherited == policy_inherited);
  assert_true(node->policy_line_nr == policy_line_nr);

  assert_true(countSubnodes(node) == subnode_count);
  if(node->subnodes == NULL)
  {
    assert_true(subnode_count == 0);
    assert_true(subnodes_contain_regex == false);
    assert_true(node->subnodes_contain_regex == false);
  }
  else
  {
    assert_true(node->subnodes_contain_regex == subnodes_contain_regex);
  }

  assert_true(node->ignore_matcher_list != NULL);
}

/** Extends checkBasicNode() with root node specific checks by wrapping it.
*/
static void checkRootNode(SearchNode *node, size_t subnode_count,
                          bool subnodes_contain_regex, BackupPolicy policy,
                          size_t policy_line_nr, size_t ignore_matcher_count)
{
  checkBasicNode(node, subnode_count, subnodes_contain_regex,
                 policy, false, policy_line_nr);

  assert_true(node->matcher == NULL);
  assert_true(countIgnoreMatcher(node) == ignore_matcher_count);
  assert_true(node->next == NULL);
}

/** Extends checkBasicNode() with more values to check for. This function
  takes the following additional arguments:

  @param root_node The root node of the tree to which the given node
  belongs to.
  @param matcher_line_nr The number of the line in the config file on which
  the nodes matcher expression was initially defined.
  @param matcher_string The matcher expression as a string.
*/
static void checkNode(SearchNode *node, SearchNode *root_node,
                      size_t subnode_count, bool subnodes_contain_regex,
                      BackupPolicy policy, bool policy_inherited,
                      size_t policy_line_nr, size_t matcher_line_nr,
                      const char *matcher_string)
{
  checkBasicNode(node, subnode_count, subnodes_contain_regex,
                 policy, policy_inherited, policy_line_nr);

  assert_true(node->matcher != NULL);
  assert_true(strmatchHasMatched(node->matcher) == false);
  assert_true(strmatchLineNr(node->matcher) == matcher_line_nr);
  assert_true(strCompare(strmatchGetExpression(node->matcher),
                         str(matcher_string)));

  assert_true(node->ignore_matcher_list == root_node->ignore_matcher_list);
}

/** Loads a search tree from a simple config file and checks it.

  @param path A null-terminated string, containing a path to a valid config
  file.
*/
static void testSimpleConfigFile(const char *path)
{
  SearchNode *root = searchTreeLoad(path);
  checkRootNode(root, 2, false, BPOL_none, 0, 0);

  SearchNode *home = findSubnode(root, "home");
  checkNode(home, root, 2, false, BPOL_none, false, 2, 2, "home");

  checkNode(findSubnode(home, "foo"),
            root, 0, false, BPOL_mirror, false, 5, 5, "foo");

  SearchNode *user = findSubnode(home, "user");
  checkNode(user, root, 2, false, BPOL_none, false, 2, 2, "user");

  checkNode(findSubnode(user, "Pictures"),
            root, 0, false, BPOL_copy, false, 2, 2, "Pictures");

  checkNode(findSubnode(user, ".config"),
            root, 0, false, BPOL_track, false, 9, 9, ".config");

  checkNode(findSubnode(root, "etc"),
            root, 0, false, BPOL_track, false, 8, 8, "etc");
}

/** Test parsing the config file "inheritance-1.txt". */
static void testInheritance_1(void)
{
  SearchNode *root = searchTreeLoad("config-files/inheritance-1.txt");
  checkRootNode(root, 1, false, BPOL_track, 14, 0);

  SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, 1, false, BPOL_mirror, false, 11, 2, "usr");

  SearchNode *portage = findSubnode(usr, "portage");
  checkNode(portage, root, 1, false, BPOL_copy, false, 8, 2, "portage");

  SearchNode *app_crypt = findSubnode(portage, "app-crypt");
  checkNode(app_crypt, root, 1, false, BPOL_copy, true, 8, 2, "app-crypt");

  SearchNode *seahorse = findSubnode(app_crypt, "seahorse");
  checkNode(seahorse, root, 1, true, BPOL_mirror, false, 5, 2, "seahorse");

  checkNode(findSubnode(seahorse, ".*\\.ebuild"),
            root, 0, false, BPOL_copy, false, 2, 2, ".*\\.ebuild");
}

/** Test parsing the config file "inheritance-2.txt". */
static void testInheritance_2(void)
{
  SearchNode *root = searchTreeLoad("config-files/inheritance-2.txt");
  checkRootNode(root, 1, false, BPOL_copy, 3, 3);
  assert_true(ignoreMatcherExists(root, "foo"));
  assert_true(ignoreMatcherExists(root, "^ "));
  assert_true(ignoreMatcherExists(root, "bar"));

  SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, 1, false, BPOL_mirror, false, 6, 6, "usr");

  SearchNode *portage = findSubnode(usr, "portage");
  checkNode(portage, root, 1, false, BPOL_track, false, 15, 15, "portage");

  SearchNode *app_crypt = findSubnode(portage, "app-crypt");
  checkNode(app_crypt, root, 1, false, BPOL_track, true, 18, 18, "app-crypt");

  SearchNode *seahorse = findSubnode(app_crypt, "seahorse");
  checkNode(seahorse, root, 1, true, BPOL_copy, false, 18, 18, "seahorse");

  SearchNode *ebuild = findSubnode(seahorse, ".*\\.ebuild");
  checkNode(ebuild, root, 0, false, BPOL_mirror, false, 21, 21, ".*\\.ebuild");
}

/** Tests parsing the config file "inheritance-3.txt". */
static void testInheritance_3(void)
{
  SearchNode *root = searchTreeLoad("config-files/inheritance-3.txt");
  checkRootNode(root, 2, false, BPOL_none, 0, 2);
  assert_true(ignoreMatcherExists(root, ".*\\.png"));
  assert_true(ignoreMatcherExists(root, ".*\\.jpg"));

  SearchNode *home_1 = findSubnode(root, "home");
  checkNode(home_1, root, 1, false, BPOL_mirror, false, 28, 22, "home");

  SearchNode *user_1 = findSubnode(home_1, "user");
  checkNode(user_1, root, 1, false, BPOL_mirror, true, 28, 22, "user");

  SearchNode *config_1 = findSubnode(user_1, ".config");
  checkNode(config_1, root, 3, true, BPOL_mirror, true, 28, 22, ".config");

  SearchNode *dlaunch_1 = findSubnode(config_1, "dlaunch");
  checkNode(dlaunch_1, root, 1, false, BPOL_mirror, true, 28, 22, "dlaunch");

  checkNode(findSubnode(dlaunch_1, "ignore-files.txt"),
            root, 0, false, BPOL_track, false, 22, 22, "ignore-files.txt");

  SearchNode *htop_1 = findSubnode(config_1, "htop");
  checkNode(htop_1, root, 1, false, BPOL_mirror, true, 28, 23, "htop");

  checkNode(findSubnode(htop_1, "htoprc"),
            root, 0, false, BPOL_track, false, 23, 23, "htoprc");

  checkNode(findSubnode(config_1, ".*\\.conf"),
            root, 0, false, BPOL_track, false, 24, 24, ".*\\.conf");

  SearchNode *usr_1 = findSubnode(root, "usr");
  checkNode(usr_1, root, 1, false, BPOL_none, false, 27, 27, "usr");

  SearchNode *portage_1 = findSubnode(usr_1, "portage");
  checkNode(portage_1, root, 1, true, BPOL_none, false, 27, 27, "portage");

  checkNode(findSubnode(portage_1, "(distfiles|packages)"),
            root, 0, false, BPOL_mirror, false, 27, 27, "(distfiles|packages)");
}

int main(void)
{
  testGroupStart("various config files");
  testInheritance_1();
  testInheritance_2();
  testInheritance_3();
  testGroupEnd();

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
