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
  Tests loading search trees from config files.
*/

#include "search-tree.h"

#include <stdlib.h>

#include "test.h"
#include "path-builder.h"
#include "string-utils.h"
#include "safe-wrappers.h"
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

/** Returns the subnode with the given string as its name. It will
  terminate the test suite with failure if the node does not exist.

  @param parent_node The node containing the subnodes which should be
  searched.
  @param string The name of the node to search for.

  @return A valid search node.
*/
static SearchNode *findSubnode(SearchNode *parent_node, const char *string)
{
  String node_name = str(string);
  for(SearchNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
  {
    if(strCompare(node->name, node_name))
    {
      return node;
    }
  }

  die("failed to find node with name \"%s\"", string);

  /* Some compilers are not aware that die() never returns. */
  return NULL;
}

/** Counts all ignore expressions that the given SearchNode contains.

  @param node The SearchNode containing the ignore expression list.

  @return The amount of ignore expressions in the given nodes ignore
  expression list.
*/
static size_t countIgnoreExpressions(SearchNode *node)
{
  size_t counter = 0;

  for(RegexList *expression = *node->ignore_expressions;
      expression != NULL; expression = expression->next)
  {
    counter++;
  }

  return counter;
}

/** Returns true, if a valid ignore expression with the specified
  properties exists in the given SearchNode.

  @param node The node containing the ignore expression list.
  @param pattern The pattern which should be searched for.
  @param line_nr The number of the line in the config file, on which the
  given pattern was defined initially.

  @return True, if a pattern with the specified properties exists in the
  ignore expression list.
*/
static bool checkIgnoreExpression(SearchNode *node, const char *pattern,
                                  size_t line_nr)
{
  String expression_string = str(pattern);
  for(RegexList *expression = *node->ignore_expressions;
      expression != NULL; expression = expression->next)
  {
    if(expression->has_matched == false &&
       expression->line_nr == line_nr &&
       strCompare(expression->expression, expression_string) &&
       expression->expression.str[expression->expression.length] == '\0')
    {
      return true;
    }
  }

  return false;
}

/** Returns true, if the given node contains at least one subnode with a
  regex. */
static bool subnodesContainRegex(SearchNode *parent_node)
{
  for(SearchNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
  {
    if(node->regex != NULL)
    {
      return true;
    }
  }

  return false;
}

/** Checks if the given node contains the given values.

  @param node The node that should be checked.
  @param name The name of the node.
  @param line_nr The number of the line, on which the node was initially
  defined in the config file.
  @param has_regex True, if the node must have a compiled regex.
  @param policy The policy of the node.
  @param policy_inherited True, if the node has inherited its policy.
  @param policy_line_nr The number of the line in the config file on which
  the node has got its policy assigned to it.
  @param subnode_count The amount of subnodes in the node.
  @param subnodes_contain_regex True, if at least one subnode contains a
  regex.
*/
static void checkBasicNode(SearchNode *node, const char *name,
                           size_t line_nr, bool has_regex,
                           BackupPolicy policy, bool policy_inherited,
                           size_t policy_line_nr, size_t subnode_count,
                           bool subnodes_contain_regex)
{
  assert_true(node != NULL);

  assert_true(strCompare(node->name, str(name)));
  assert_true(node->name.str[node->name.length] == '\0');
  assert_true(node->line_nr == line_nr);

  if(has_regex)
  {
    assert_true(node->regex != NULL);
  }
  else
  {
    assert_true(node->regex == NULL);
  }

  assert_true(node->search_match == SRT_none);
  assert_true(node->policy == policy);
  assert_true(node->policy_inherited == policy_inherited);
  assert_true(node->policy_line_nr == policy_line_nr);

  assert_true(countSubnodes(node) == subnode_count);
  if(node->subnodes == NULL)
  {
    assert_true(subnode_count == 0);
    assert_true(subnodes_contain_regex == false);
  }

  assert_true(node->subnodes_contain_regex == subnodes_contain_regex);
  assert_true(node->subnodes_contain_regex == subnodesContainRegex(node));
  assert_true(node->ignore_expressions != NULL);
}

/** Extends checkBasicNode() with root node specific checks by wrapping it.
*/
static void checkRootNode(SearchNode *node, BackupPolicy policy,
                          size_t policy_line_nr, size_t subnode_count,
                          bool subnodes_contain_regex,
                          size_t ignore_expression_count)
{
  checkBasicNode(node, "/", 0, false, policy, false, policy_line_nr,
                 subnode_count, subnodes_contain_regex);

  assert_true(countIgnoreExpressions(node) == ignore_expression_count);
  assert_true(node->next == NULL);
}

/** Extends checkBasicNode() with some additional checks by wrapping it.
  Here are the added parameters:

  @param root_node The root node of the tree, to which the given node
  belongs.
*/
static void checkNode(SearchNode *node, SearchNode *root_node,
                      const char *name, size_t line_nr, bool has_regex,
                      BackupPolicy policy, bool policy_inherited,
                      size_t policy_line_nr, size_t subnode_count,
                      bool subnodes_contain_regex)
{
  checkBasicNode(node, name, line_nr, has_regex, policy, policy_inherited,
                 policy_line_nr, subnode_count, subnodes_contain_regex);

  assert_true(node->ignore_expressions == root_node->ignore_expressions);
}

/** Loads a search tree from a simple config file and checks it.

  @param path A null-terminated string, containing a path to a valid config
  file.
*/
static void testSimpleConfigFile(const char *path)
{
  SearchNode *root = searchTreeLoad(path);
  checkRootNode(root, BPOL_none, 0, 2, false, 0);

  SearchNode *home = findSubnode(root, "home");
  checkNode(home, root, "home", 2, false, BPOL_none, false, 2, 2, false);

  checkNode(findSubnode(home, "foo"),
            root, "foo", 5, false, BPOL_mirror, false, 5, 0, false);

  SearchNode *user = findSubnode(home, "user");
  checkNode(user, root, "user", 2, false, BPOL_none, false, 2, 2, false);

  checkNode(findSubnode(user, "Pictures"),
            root, "Pictures", 2, false, BPOL_copy, false, 2, 0, false);

  checkNode(findSubnode(user, ".config"),
            root, ".config", 9, false, BPOL_track, false, 9, 0, false);

  checkNode(findSubnode(root, "etc"),
            root, "etc", 8, false, BPOL_track, false, 8, 0, false);
}

/** Test parsing the config file "inheritance-1.txt". */
static void testInheritance_1(void)
{
  SearchNode *root = searchTreeLoad("valid-config-files/inheritance-1.txt");
  checkRootNode(root, BPOL_track, 14, 1, false, 0);

  SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, "usr", 2, false, BPOL_mirror, false, 11, 1, false);

  SearchNode *portage = findSubnode(usr, "portage");
  checkNode(portage, root, "portage", 2, false, BPOL_copy, false, 8, 1, false);

  SearchNode *app_crypt = findSubnode(portage, "app-crypt");
  checkNode(app_crypt, root, "app-crypt", 2, false, BPOL_copy, true, 8, 1, false);

  SearchNode *seahorse = findSubnode(app_crypt, "seahorse");
  checkNode(seahorse, root, "seahorse", 2, false, BPOL_mirror, false, 5, 1, true);

  checkNode(findSubnode(seahorse, ".*\\.ebuild"),
            root, ".*\\.ebuild", 2, true, BPOL_copy, false, 2, 0, false);
}

/** Test parsing the config file "inheritance-2.txt". */
static void testInheritance_2(void)
{
  SearchNode *root = searchTreeLoad("valid-config-files/inheritance-2.txt");
  checkRootNode(root, BPOL_copy, 3, 1, false, 3);
  assert_true(checkIgnoreExpression(root, "foo", 9));
  assert_true(checkIgnoreExpression(root, "^ ",  10));
  assert_true(checkIgnoreExpression(root, "bar", 11));

  SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, "usr", 15, false, BPOL_copy, true, 15, 1, false);

  SearchNode *portage = findSubnode(usr, "portage");
  checkNode(portage, root, "portage", 15, false, BPOL_track, false, 15, 1, false);

  SearchNode *app_crypt = findSubnode(portage, "app-crypt");
  checkNode(app_crypt, root, "app-crypt", 18, false, BPOL_track, true, 18, 1, false);

  SearchNode *seahorse = findSubnode(app_crypt, "seahorse");
  checkNode(seahorse, root, "seahorse", 18, false, BPOL_copy, false, 18, 1, true);

  SearchNode *ebuild = findSubnode(seahorse, ".*\\.ebuild");
  checkNode(ebuild, root, ".*\\.ebuild", 21, true, BPOL_mirror, false, 21, 0, false);
}

/** Tests parsing the config file "inheritance-3.txt". */
static void testInheritance_3(void)
{
  SearchNode *root = searchTreeLoad("valid-config-files/inheritance-3.txt");
  checkRootNode(root, BPOL_none, 0, 2, false, 2);
  assert_true(checkIgnoreExpression(root, ".*\\.png", 14));
  assert_true(checkIgnoreExpression(root, ".*\\.jpg", 16));

  SearchNode *home_1 = findSubnode(root, "home");
  checkNode(home_1, root, "home", 22, false, BPOL_mirror, false, 28, 1, false);

  SearchNode *user_1 = findSubnode(home_1, "user");
  checkNode(user_1, root, "user", 22, false, BPOL_mirror, true, 28, 1, false);

  SearchNode *config_1 = findSubnode(user_1, ".config");
  checkNode(config_1, root, ".config", 22, false, BPOL_mirror, true, 28, 3, true);

  SearchNode *dlaunch_1 = findSubnode(config_1, "dlaunch");
  checkNode(dlaunch_1, root, "dlaunch", 22, false, BPOL_mirror, true, 28, 1, false);

  checkNode(findSubnode(dlaunch_1, "ignore-files.txt"),
            root, "ignore-files.txt", 22, false, BPOL_track, false, 22, 0, false);

  SearchNode *htop_1 = findSubnode(config_1, "htop");
  checkNode(htop_1, root, "htop", 23, false, BPOL_mirror, true, 28, 1, false);

  checkNode(findSubnode(htop_1, "htoprc"),
            root, "htoprc", 23, false, BPOL_track, false, 23, 0, false);

  checkNode(findSubnode(config_1, ".*\\.conf"),
            root, ".*\\.conf", 24, true, BPOL_track, false, 24, 0, false);

  SearchNode *usr_1 = findSubnode(root, "usr");
  checkNode(usr_1, root, "usr", 27, false, BPOL_none, false, 27, 1, false);

  SearchNode *portage_1 = findSubnode(usr_1, "portage");
  checkNode(portage_1, root, "portage", 27, false, BPOL_none, false, 27, 1, true);

  checkNode(findSubnode(portage_1, "(distfiles|packages)"),
            root, "(distfiles|packages)", 27, true, BPOL_mirror, false, 27, 0, false);
}

/** Tests parsing the config file "root-with-regex-subnodes.txt". */
static void testRootWithRegexSubnodes(void)
{
  SearchNode *root = searchTreeLoad("valid-config-files/root-with-regex-subnodes.txt");
  checkRootNode(root, BPOL_none, 0, 3, true, 0);

  checkNode(findSubnode(root, "\\.txt$"),
            root, "\\.txt$", 2, true, BPOL_copy, false, 2, 0, false);
  checkNode(findSubnode(root, "foo"),
            root, "foo", 5, false, BPOL_mirror, false, 5, 0, false);
  checkNode(findSubnode(root, "(foo-)?bar$"),
            root, "(foo-)?bar$", 6, true, BPOL_mirror, false, 6, 0, false);
}

/** Tests parsing the config file "paths with whitespaces.txt" */
static void testPathsWithWhitespaces(void)
{
  SearchNode *root = searchTreeLoad("valid-config-files/paths with whitespaces.txt");
  checkRootNode(root, BPOL_none, 0, 1, false, 0);

  SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, "usr", 2, false, BPOL_none, false, 2, 2, true);
  checkNode(findSubnode(usr, " ^foobar "),
            root, " ^foobar ", 3, true, BPOL_copy, false, 3, 0, false);

  SearchNode *foo = findSubnode(usr, "foo ");
  checkNode(foo, root, "foo ", 2, false, BPOL_copy, false, 2, 1, false);

  SearchNode *bar = findSubnode(foo, " bar ");
  checkNode(bar, root, " bar ", 6, false, BPOL_copy, true, 6, 1, false);
  checkNode(findSubnode(bar, "foo bar "),
            root, "foo bar ", 6, false, BPOL_mirror, false, 6, 0, false);
}

/** Asserts that parsing the given config file results in the given error
  message.

  @param path The path to the config file to parse.
  @param message The error message to expect.
*/
static void assertParseError(const char *path, const char *message)
{
  FileContent content = sGetFilesContent(path);
  String config = (String)
  {
    .str = content.content,
    .length = content.size,
  };

  assert_error(searchTreeParse(config), message);

  free(content.content);
}

/** Loads the given config file, sets various bytes to '\0' and passes it
  to searchTreeParse().

  @param path The path to the config file.
*/
static void testInsertNullBytes(const char *path)
{
  FileContent content = sGetFilesContent(path);
  if(content.size == 0)
  {
    return;
  }

  String config = { .str = content.content, .length = content.size };

  for(size_t index = 0; index < content.size; index++)
  {
    const char old_char = content.content[index];
    content.content[index] = '\0';

    assert_error(searchTreeParse(config), "config: contains null-bytes");

    content.content[index] = old_char;
  }

  free(content.content);
}

/** Searches for config files in various directories and passes them to
  testInsertNullBytes(). */
static void testNullBytesConfigFiles(void)
{
  static Buffer *buffer = NULL;
  const char *config_paths[] =
  {
    "broken-config-files",
    "generated-config-files",
    "template-config-files",
    "valid-config-files",
  };

  for(size_t dir_index = 0;
      dir_index < sizeof(config_paths)/sizeof(config_paths[0]);
      dir_index++)
  {
    const char *dir_path = config_paths[dir_index];
    const size_t path_length = pathBuilderSet(&buffer, dir_path);
    DIR *dir = sOpenDir(dir_path);

    for(struct dirent *dir_entry = sReadDir(dir, dir_path);
        dir_entry != NULL; dir_entry = sReadDir(dir, dir_path))
    {
      pathBuilderAppend(&buffer, path_length, dir_entry->d_name);
      testInsertNullBytes(buffer->data);
    }

    sCloseDir(dir, dir_path);
  }
}

int main(void)
{
  testGroupStart("various config files");
  testInheritance_1();
  testInheritance_2();
  testInheritance_3();
  testRootWithRegexSubnodes();
  testPathsWithWhitespaces();

  checkRootNode(searchTreeLoad("empty.txt"), BPOL_none, 0, 0, false, 0);
  checkRootNode(searchTreeLoad("valid-config-files/no-paths-and-no-ignores.txt"),
                BPOL_none, 0, 0, false, 0);

  SearchNode *ignore_1 = searchTreeLoad("valid-config-files/ignore-patterns-only-1.txt");
  checkRootNode(ignore_1, BPOL_none, 0, 0, false, 2);
  assert_true(checkIgnoreExpression(ignore_1, " .*\\.(png|jpg|pdf) ", 2));
  assert_true(checkIgnoreExpression(ignore_1, "foo", 3));

  SearchNode *ignore_2 = searchTreeLoad("valid-config-files/ignore-patterns-only-2.txt");
  checkRootNode(ignore_2, BPOL_none, 0, 0, false, 4);
  assert_true(checkIgnoreExpression(ignore_2, "foo",      7));
  assert_true(checkIgnoreExpression(ignore_2, "bar",      9));
  assert_true(checkIgnoreExpression(ignore_2, "foo-bar",  12));
  assert_true(checkIgnoreExpression(ignore_2, ".*\\.png", 17));
  testGroupEnd();

  testGroupStart("BOM and EOL variations");
  testSimpleConfigFile("valid-config-files/simple.txt");
  testSimpleConfigFile("valid-config-files/simple-BOM.txt");
  testSimpleConfigFile("valid-config-files/simple-noeol.txt");
  testSimpleConfigFile("valid-config-files/simple-BOM-noeol.txt");
  testGroupEnd();

  testGroupStart("broken config files");
  assert_error(searchTreeLoad("non-existing-file.txt"), "failed to access "
               "\"non-existing-file.txt\": No such file or directory");

  assertParseError("broken-config-files/invalid-policy.txt",
                   "config: line 7: invalid policy: \"trak\"");

  assertParseError("broken-config-files/empty-policy-name.txt",
                   "config: line 9: invalid policy: \"\"");

  assertParseError("broken-config-files/opening-brace.txt",
                   "config: line 6: invalid path: \"[foo\"");

  assertParseError("broken-config-files/opening-brace-empty.txt",
                   "config: line 9: invalid path: \"[\"");

  assertParseError("broken-config-files/closing-brace.txt",
                   "config: line 7: invalid path: \"foo]\"");

  assertParseError("broken-config-files/closing-brace-empty.txt",
                   "config: line 3: invalid path: \"]\"");

  assertParseError("broken-config-files/invalid-regex.txt",
                   "config: line 5: Unmatched ( or \\(: \"(foo|bar\"");

  assertParseError("broken-config-files/pattern-without-policy.txt",
                   "config: line 8: pattern without policy: \"/home/user/foo/bar.txt\"");

  assertParseError("broken-config-files/invalid-ignore-expression.txt",
                   "config: line 6: Unmatched [ or [^: \" ([0-9A-Za-z)+///\"");

  assertParseError("broken-config-files/redefine-1.txt",
                   "config: line 6: redefining line 4: \"/home/user/foo/Gentoo Packages/\"");

  assertParseError("broken-config-files/redefine-2.txt",
                   "config: line 12: redefining line 6: \"/home/user/foo/Packages\"");

  assertParseError("broken-config-files/redefine-3.txt",
                   "config: line 24: redefining line 12: \"/home/\"");

  assertParseError("broken-config-files/redefine-root-1.txt",
                   "config: line 11: redefining line 7: \"/\"");

  assertParseError("broken-config-files/redefine-root-2.txt",
                   "config: line 17: redefining line 9: \"/\"");

  assertParseError("broken-config-files/redefine-policy-1.txt",
                   "config: line 8: redefining policy of line 4: \"/home/user/.config /\"");

  assertParseError("broken-config-files/redefine-policy-2.txt",
                   "config: line 21: redefining policy of line 12: \"/home/user/\"");

  assertParseError("broken-config-files/redefine-root-policy-1.txt",
                   "config: line 5: redefining policy of line 2: \"/\"");

  assertParseError("broken-config-files/redefine-root-policy-2.txt",
                   "config: line 15: redefining policy of line 6: \"/\"");

  assertParseError("broken-config-files/invalid-path-1.txt",
                   "config: line 9: invalid path: \"     /foo/bar\"");

  assertParseError("broken-config-files/invalid-path-2.txt",
                   "config: line 3: invalid path: \"~/.bashrc\"");

  assertParseError("broken-config-files/invalid-path-3.txt",
                   "config: line 7: invalid path: \".bash_history\"");

  assertParseError("broken-config-files/multiple-errors.txt",
                   "config: line 9: Invalid preceding regular expression: \"???*\"");

  assertParseError("broken-config-files/BOM-simple-error.txt",
                   "config: line 3: invalid path: \"This file contains a UTF-8 BOM.\"");
  testGroupEnd();

  testGroupStart("config files containing null-bytes");
  testNullBytesConfigFiles();
  testGroupEnd();
}
