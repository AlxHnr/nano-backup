#include "search-tree.h"

#include <stdlib.h>

#include "error-handling.h"
#include "safe-wrappers.h"
#include "str.h"
#include "test.h"

static size_t countSubnodes(const SearchNode *parent_node)
{
  size_t counter = 0;

  for(const SearchNode *node = parent_node->subnodes; node != NULL; node = node->next)
  {
    counter++;
  }

  return counter;
}

/** Returns the subnode with the given string as its name. It will
  terminate the test suite with failure if the node does not exist. */
static const SearchNode *findSubnode(const SearchNode *parent_node, const char *string)
{
  StringView node_name = str(string);
  for(const SearchNode *node = parent_node->subnodes; node != NULL; node = node->next)
  {
    if(strEqual(node->name, node_name))
    {
      return node;
    }
  }

  die("failed to find node with name \"%s\"", string);

  /* Some compilers are not aware that die() never returns. */
  return NULL;
}

/** Counts all ignore expressions that the given SearchNode contains. */
static size_t countExpressions(const RegexList *expression_list)
{
  size_t counter = 0;

  for(const RegexList *expression = expression_list; expression != NULL; expression = expression->next)
  {
    counter++;
  }

  return counter;
}

/** Returns true, if a valid expression with the specified properties
  exists in the given expression list.

  @param expression_list List to check.
  @param pattern The pattern which should be searched for.
  @param line_nr The number of the line in the config file, on which the
  given pattern was defined initially.

  @return True, if a pattern with the specified properties exists in the
  given expression list.
*/
static bool checkExpressionList(const RegexList *expression_list, const char *pattern, const size_t line_nr)
{
  StringView expression_string = str(pattern);
  for(const RegexList *expression = expression_list; expression != NULL; expression = expression->next)
  {
    if(expression->has_matched == false && expression->line_nr == line_nr &&
       strEqual(expression->expression, expression_string))
    {
      return true;
    }
  }

  return false;
}

static bool checkIgnoreExpression(const SearchNode *node, const char *pattern, const size_t line_nr)
{
  return checkExpressionList(*node->ignore_expressions, pattern, line_nr);
}

static bool checkSummarizeExpression(const SearchNode *node, const char *pattern, const size_t line_nr)
{
  return checkExpressionList(*node->summarize_expressions, pattern, line_nr);
}

/** Returns true, if the given node contains at least one subnode with a
  regex. */
static bool subnodesContainRegex(const SearchNode *parent_node)
{
  for(const SearchNode *node = parent_node->subnodes; node != NULL; node = node->next)
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
static void checkBasicNode(const SearchNode *node, const char *name, const size_t line_nr, const bool has_regex,
                           const BackupPolicy policy, const bool policy_inherited, const size_t policy_line_nr,
                           const size_t subnode_count, const bool subnodes_contain_regex)
{
  assert_true(node != NULL);

  assert_true(strEqual(node->name, str(name)));
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
    assert_true(!subnodes_contain_regex);
  }

  assert_true(node->subnodes_contain_regex == subnodes_contain_regex);
  assert_true(node->subnodes_contain_regex == subnodesContainRegex(node));
  assert_true(node->ignore_expressions != NULL);
}

/** Extends checkBasicNode() with root node specific checks by wrapping it.
 */
static void checkRootNode(const SearchNode *node, const BackupPolicy policy, const size_t policy_line_nr,
                          const size_t subnode_count, const bool subnodes_contain_regex,
                          const size_t ignore_expression_count, const size_t summarize_expression_count)
{
  checkBasicNode(node, "/", 0, false, policy, false, policy_line_nr, subnode_count, subnodes_contain_regex);

  assert_true(countExpressions(*node->ignore_expressions) == ignore_expression_count);
  assert_true(countExpressions(*node->summarize_expressions) == summarize_expression_count);
  assert_true(node->next == NULL);
}

/** Extends checkBasicNode() with some additional checks by wrapping it.
  Here are the added parameters:

  @param root_node The root node of the tree, to which the given node
  belongs.
*/
static void checkNode(const SearchNode *node, const SearchNode *root_node, const char *name, const size_t line_nr,
                      const bool has_regex, const BackupPolicy policy, const bool policy_inherited,
                      const size_t policy_line_nr, const size_t subnode_count, const bool subnodes_contain_regex)
{
  checkBasicNode(node, name, line_nr, has_regex, policy, policy_inherited, policy_line_nr, subnode_count,
                 subnodes_contain_regex);

  assert_true(node->ignore_expressions == root_node->ignore_expressions);
}

/** Loads a search tree from a simple config file and checks it.

  @param path A null-terminated string, containing a path to a valid config
  file.
*/
static void testSimpleConfigFile(StringView path)
{
  const SearchNode *root = searchTreeLoad(path);
  checkRootNode(root, BPOL_none, 0, 2, false, 0, 0);

  const SearchNode *home = findSubnode(root, "home");
  checkNode(home, root, "home", 2, false, BPOL_none, false, 2, 2, false);

  checkNode(findSubnode(home, "foo"), root, "foo", 5, false, BPOL_mirror, false, 5, 0, false);

  const SearchNode *user = findSubnode(home, "user");
  checkNode(user, root, "user", 2, false, BPOL_none, false, 2, 2, false);

  checkNode(findSubnode(user, "Pictures"), root, "Pictures", 2, false, BPOL_copy, false, 2, 0, false);

  checkNode(findSubnode(user, ".config"), root, ".config", 9, false, BPOL_track, false, 9, 0, false);

  checkNode(findSubnode(root, "etc"), root, "etc", 8, false, BPOL_track, false, 8, 0, false);
}

/** Test parsing the config file "inheritance-1.txt". */
static void testInheritance1(void)
{
  const SearchNode *root = searchTreeLoad(str("valid-config-files/inheritance-1.txt"));
  checkRootNode(root, BPOL_track, 14, 1, false, 0, 0);

  const SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, "usr", 2, false, BPOL_mirror, false, 11, 1, false);

  const SearchNode *portage = findSubnode(usr, "portage");
  checkNode(portage, root, "portage", 2, false, BPOL_copy, false, 8, 1, false);

  const SearchNode *app_crypt = findSubnode(portage, "app-crypt");
  checkNode(app_crypt, root, "app-crypt", 2, false, BPOL_copy, true, 8, 1, false);

  const SearchNode *seahorse = findSubnode(app_crypt, "seahorse");
  checkNode(seahorse, root, "seahorse", 2, false, BPOL_mirror, false, 5, 1, true);

  checkNode(findSubnode(seahorse, ".*\\.ebuild"), root, ".*\\.ebuild", 2, true, BPOL_copy, false, 2, 0, false);
}

/** Test parsing the config file "inheritance-2.txt". */
static void testInheritance2(void)
{
  const SearchNode *root = searchTreeLoad(str("valid-config-files/inheritance-2.txt"));
  checkRootNode(root, BPOL_copy, 3, 1, false, 3, 0);
  assert_true(checkIgnoreExpression(root, "foo", 9));
  assert_true(checkIgnoreExpression(root, "^ ", 10));
  assert_true(checkIgnoreExpression(root, "bar", 11));

  const SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, "usr", 15, false, BPOL_copy, true, 15, 1, false);

  const SearchNode *portage = findSubnode(usr, "portage");
  checkNode(portage, root, "portage", 15, false, BPOL_track, false, 15, 1, false);

  const SearchNode *app_crypt = findSubnode(portage, "app-crypt");
  checkNode(app_crypt, root, "app-crypt", 18, false, BPOL_track, true, 18, 1, false);

  const SearchNode *seahorse = findSubnode(app_crypt, "seahorse");
  checkNode(seahorse, root, "seahorse", 18, false, BPOL_copy, false, 18, 1, true);

  const SearchNode *ebuild = findSubnode(seahorse, ".*\\.ebuild");
  checkNode(ebuild, root, ".*\\.ebuild", 21, true, BPOL_mirror, false, 21, 0, false);
}

/** Tests parsing the config file "inheritance-3.txt". */
static void testInheritance3(void)
{
  const SearchNode *root = searchTreeLoad(str("valid-config-files/inheritance-3.txt"));
  checkRootNode(root, BPOL_none, 0, 2, false, 2, 0);
  assert_true(checkIgnoreExpression(root, ".*\\.png", 14));
  assert_true(checkIgnoreExpression(root, ".*\\.jpg", 16));

  const SearchNode *home_1 = findSubnode(root, "home");
  checkNode(home_1, root, "home", 22, false, BPOL_mirror, false, 28, 1, false);

  const SearchNode *user_1 = findSubnode(home_1, "user");
  checkNode(user_1, root, "user", 22, false, BPOL_mirror, true, 28, 1, false);

  const SearchNode *config_1 = findSubnode(user_1, ".config");
  checkNode(config_1, root, ".config", 22, false, BPOL_mirror, true, 28, 3, true);

  const SearchNode *dlaunch_1 = findSubnode(config_1, "dlaunch");
  checkNode(dlaunch_1, root, "dlaunch", 22, false, BPOL_mirror, true, 28, 1, false);

  checkNode(findSubnode(dlaunch_1, "ignore-files.txt"), root, "ignore-files.txt", 22, false, BPOL_track, false, 22,
            0, false);

  const SearchNode *htop_1 = findSubnode(config_1, "htop");
  checkNode(htop_1, root, "htop", 23, false, BPOL_mirror, true, 28, 1, false);

  checkNode(findSubnode(htop_1, "htoprc"), root, "htoprc", 23, false, BPOL_track, false, 23, 0, false);

  checkNode(findSubnode(config_1, ".*\\.conf"), root, ".*\\.conf", 24, true, BPOL_track, false, 24, 0, false);

  const SearchNode *usr_1 = findSubnode(root, "usr");
  checkNode(usr_1, root, "usr", 27, false, BPOL_none, false, 27, 1, false);

  const SearchNode *portage_1 = findSubnode(usr_1, "portage");
  checkNode(portage_1, root, "portage", 27, false, BPOL_none, false, 27, 1, true);

  checkNode(findSubnode(portage_1, "(distfiles|packages)"), root, "(distfiles|packages)", 27, true, BPOL_mirror,
            false, 27, 0, false);
}

/** Tests parsing the config file "root-with-regex-subnodes.txt". */
static void testRootWithRegexSubnodes(void)
{
  const SearchNode *root = searchTreeLoad(str("valid-config-files/root-with-regex-subnodes.txt"));
  checkRootNode(root, BPOL_none, 0, 3, true, 0, 0);

  checkNode(findSubnode(root, "\\.txt$"), root, "\\.txt$", 2, true, BPOL_copy, false, 2, 0, false);
  checkNode(findSubnode(root, "foo"), root, "foo", 5, false, BPOL_mirror, false, 5, 0, false);
  checkNode(findSubnode(root, "(foo-)?bar$"), root, "(foo-)?bar$", 6, true, BPOL_mirror, false, 6, 0, false);
}

/** Tests parsing the config file "paths with whitespaces.txt" */
static void testPathsWithWhitespaces(void)
{
  const SearchNode *root = searchTreeLoad(str("valid-config-files/paths with whitespaces.txt"));
  checkRootNode(root, BPOL_none, 0, 1, false, 0, 0);

  const SearchNode *usr = findSubnode(root, "usr");
  checkNode(usr, root, "usr", 2, false, BPOL_none, false, 2, 2, true);
  checkNode(findSubnode(usr, " ^foobar "), root, " ^foobar ", 3, true, BPOL_copy, false, 3, 0, false);

  const SearchNode *foo = findSubnode(usr, "foo ");
  checkNode(foo, root, "foo ", 2, false, BPOL_copy, false, 2, 1, false);

  const SearchNode *bar = findSubnode(foo, " bar ");
  checkNode(bar, root, " bar ", 6, false, BPOL_copy, true, 6, 1, false);
  checkNode(findSubnode(bar, "foo bar "), root, "foo bar ", 6, false, BPOL_mirror, false, 6, 0, false);
}

/** Tests parsing "valid-config-files/comment.txt". */
static void testIgnoringComments(void)
{
  const SearchNode *root = searchTreeLoad(str("valid-config-files/comment.txt"));
  checkRootNode(root, BPOL_none, 0, 1, false, 2, 0);

  const SearchNode *etc = findSubnode(root, "#etc");
  checkNode(etc, root, "#etc", 10, false, BPOL_none, false, 10, 1, false);

  const SearchNode *portage = findSubnode(etc, "portage");
  checkNode(portage, root, "portage", 10, false, BPOL_copy, false, 10, 1, true);

  checkNode(findSubnode(portage, "^make.conf$"), root, "^make.conf$", 24, true, BPOL_track, false, 24, 0, false);

  assert_true(checkIgnoreExpression(root, " # Pattern 1.", 16));
  assert_true(checkIgnoreExpression(root, "   # Pattern 2. ", 20));
}

/** Asserts that parsing the given config file results in the given error
  message.

  @param path The path to the config file to parse.
  @param message The error message to expect.
*/
static void assertParseError(const char *path, const char *message)
{
  CR_Region *r = CR_RegionNew();
  const FileContent content = sGetFilesContent(r, str(path));
  StringView config = { .content = content.content, .length = content.size };

  assert_error(searchTreeParse(config), message);

  CR_RegionRelease(r);
}

/** Tests loading various invalid config files. */
static void testBrokenConfigFiles(void)
{
  assert_error_errno(searchTreeLoad(str("non-existing-file.txt")), "failed to access \"non-existing-file.txt\"",
                     ENOENT);

  assertParseError("broken-config-files/invalid-policy.txt", "config: line 7: invalid policy: \"trak\"");

  assertParseError("broken-config-files/empty-policy-name.txt", "config: line 9: invalid policy: \"\"");

  assertParseError("broken-config-files/opening-brace.txt", "config: line 6: invalid path: \"[foo\"");

  assertParseError("broken-config-files/opening-brace-empty.txt", "config: line 9: invalid path: \"[\"");

  assertParseError("broken-config-files/closing-brace.txt", "config: line 7: invalid path: \"foo]\"");

  assertParseError("broken-config-files/closing-brace-empty.txt", "config: line 3: invalid path: \"]\"");

  {
    CR_Region *r = CR_RegionNew();
    char error_buffer[128];

    FileContent content = sGetFilesContent(r, str("broken-config-files/invalid-regex.txt"));
    assert_error_any(searchTreeParse(strUnterminated(content.content, content.size)));
    getLastErrorMessage(error_buffer, sizeof(error_buffer));
    assert_true(strstr(error_buffer, "config: line 5: ") == error_buffer);

    content = sGetFilesContent(r, str("broken-config-files/invalid-ignore-expression.txt"));
    assert_error_any(searchTreeParse(strUnterminated(content.content, content.size)));
    getLastErrorMessage(error_buffer, sizeof(error_buffer));
    assert_true(strstr(error_buffer, "config: line 6: ") == error_buffer);

    content = sGetFilesContent(r, str("broken-config-files/invalid-summarize-expression.txt"));
    assert_error_any(searchTreeParse(strUnterminated(content.content, content.size)));
    getLastErrorMessage(error_buffer, sizeof(error_buffer));
    assert_true(strstr(error_buffer, "config: line 8: ") == error_buffer);

    content = sGetFilesContent(r, str("broken-config-files/multiple-errors.txt"));
    assert_error_any(searchTreeParse(strUnterminated(content.content, content.size)));
    getLastErrorMessage(error_buffer, sizeof(error_buffer));
    assert_true(strstr(error_buffer, "config: line 9: ") == error_buffer);

    CR_RegionRelease(r);
  }

  assertParseError("broken-config-files/pattern-without-policy.txt",
                   "config: line 8: pattern without policy: \"/home/user/foo/bar.txt\"");

  assertParseError("broken-config-files/redefine-1.txt",
                   "config: line 6: redefining line 4: \"/home/user/foo/Gentoo Packages/\"");

  assertParseError("broken-config-files/redefine-2.txt",
                   "config: line 12: redefining line 6: \"/home/user/foo/Packages\"");

  assertParseError("broken-config-files/redefine-3.txt", "config: line 24: redefining line 12: \"/home/\"");

  assertParseError("broken-config-files/redefine-root-1.txt", "config: line 11: redefining line 7: \"/\"");

  assertParseError("broken-config-files/redefine-root-2.txt", "config: line 17: redefining line 9: \"/\"");

  assertParseError("broken-config-files/redefine-policy-1.txt",
                   "config: line 8: redefining policy of line 4: \"/home/user/.config /\"");

  assertParseError("broken-config-files/redefine-policy-2.txt",
                   "config: line 21: redefining policy of line 12: \"/home/user/\"");

  assertParseError("broken-config-files/redefine-root-policy-1.txt",
                   "config: line 5: redefining policy of line 2: \"/\"");

  assertParseError("broken-config-files/redefine-root-policy-2.txt",
                   "config: line 15: redefining policy of line 6: \"/\"");

  assertParseError("broken-config-files/invalid-path-1.txt", "config: line 9: invalid path: \"     /foo/bar\"");

  assertParseError("broken-config-files/invalid-path-2.txt", "config: line 3: invalid path: \"~/.bashrc\"");

  assertParseError("broken-config-files/invalid-path-3.txt", "config: line 7: invalid path: \".bash_history\"");

  assertParseError("broken-config-files/BOM-simple-error.txt",
                   "config: line 3: invalid path: \"This file contains a UTF-8 BOM.\"");

  assertParseError("broken-config-files/path-containing-dot-1.txt",
                   "config: line 5: path contains \".\" or \"..\": \"/foo/bar/./test.txt\"");

  assertParseError("broken-config-files/path-containing-dot-2.txt",
                   "config: line 5: redefining policy of line 2: \"/misc/\"");

  assertParseError("broken-config-files/path-containing-dot-3.txt",
                   "config: line 19: path contains \".\" or \"..\": \"/.\"");

  assertParseError("broken-config-files/path-containing-dot-4.txt", "config: line 2: invalid path: \"./foo/bar\"");

  assertParseError("broken-config-files/path-containing-dot-5.txt",
                   "config: line 8: path contains \".\" or \"..\": \"/home/.../foo/bar/.\"");

  assertParseError("broken-config-files/path-containing-dot-6.txt",
                   "config: line 13: path contains \".\" or \"..\": \"/home/./foo/./bar/..\"");

  assertParseError("broken-config-files/path-containing-dot-7.txt",
                   "config: line 2: path contains \".\" or \"..\": \"/broken/./path\"");

  assertParseError("broken-config-files/path-containing-dot-8.txt",
                   "config: line 4: path contains \".\" or \"..\": \"/home/user/./foo////\"");

  assertParseError("broken-config-files/path-containing-dotdot-1.txt",
                   "config: line 5: path contains \".\" or \"..\": \"/foo/bar/../test.txt\"");

  assertParseError("broken-config-files/path-containing-dotdot-2.txt",
                   "config: line 5: redefining policy of line 2: \"/misc/\"");

  assertParseError("broken-config-files/path-containing-dotdot-3.txt",
                   "config: line 19: path contains \".\" or \"..\": \"/..\"");

  assertParseError("broken-config-files/path-containing-dotdot-4.txt",
                   "config: line 2: invalid path: \"../foo/bar\"");

  assertParseError("broken-config-files/path-containing-dotdot-5.txt",
                   "config: line 8: path contains \".\" or \"..\": \"/home/.../foo/bar/..\"");

  assertParseError("broken-config-files/path-containing-dotdot-6.txt",
                   "config: line 13: path contains \".\" or \"..\": \"/home/../foo/../bar/.\"");

  assertParseError("broken-config-files/path-containing-dotdot-7.txt",
                   "config: line 2: path contains \".\" or \"..\": \"/broken/../path\"");

  assertParseError("broken-config-files/path-containing-dotdot-8.txt",
                   "config: line 4: path contains \".\" or \"..\": \"/home/user/../foo////\"");

  assertParseError("broken-config-files/invalid-comment-1.txt", "config: line 11: invalid path: \" # bar. \"");

  assertParseError("broken-config-files/invalid-comment-2.txt",
                   "config: line 3: pattern without policy: \"   # Comment without policy.\"");
}

/** Loads the given config file, sets various bytes to '\0' and passes it
  to searchTreeParse().

  @param path The path to the config file.
*/
static void testInsertNullBytes(StringView path)
{
  CR_Region *r = CR_RegionNew();
  const FileContent content = sGetFilesContent(r, path);
  if(content.size == 0)
  {
    return;
  }

  StringView config = { .content = content.content, .length = content.size };

  for(size_t index = 0; index < content.size; index++)
  {
    const char old_char = content.content[index];
    content.content[index] = '\0';

    assert_error(searchTreeParse(config), "config: contains null-bytes");

    content.content[index] = old_char;
  }

  CR_RegionRelease(r);
}

/** Searches for config files in various directories and passes them to
  testInsertNullBytes(). */
static void testNullBytesConfigFiles(void)
{
  const char *config_paths[] = {
    "broken-config-files",
    "generated-config-files",
    "template-config-files",
    "valid-config-files",
  };

  for(size_t dir_index = 0; dir_index < sizeof(config_paths) / sizeof(config_paths[0]); dir_index++)
  {
    StringView dir_path = str(config_paths[dir_index]);
    DirIterator *dir = sDirOpen(dir_path);

    for(StringView filepath = sDirGetNext(dir); filepath.length > 0; strSet(&filepath, sDirGetNext(dir)))
    {
      testInsertNullBytes(filepath);
    }

    sDirClose(dir);
  }
}

int main(void)
{
  testGroupStart("various config files");
  testInheritance1();
  testInheritance2();
  testInheritance3();
  testRootWithRegexSubnodes();
  testPathsWithWhitespaces();

  checkRootNode(searchTreeLoad(str("empty.txt")), BPOL_none, 0, 0, false, 0, 0);
  checkRootNode(searchTreeLoad(str("valid-config-files/no-paths-and-no-ignores.txt")), BPOL_none, 0, 0, false, 0,
                0);

  const SearchNode *ignore_1 = searchTreeLoad(str("valid-config-files/ignore-patterns-only-1.txt"));
  checkRootNode(ignore_1, BPOL_none, 0, 0, false, 2, 0);
  assert_true(checkIgnoreExpression(ignore_1, " .*\\.(png|jpg|pdf) ", 2));
  assert_true(checkIgnoreExpression(ignore_1, "foo", 3));

  const SearchNode *ignore_2 = searchTreeLoad(str("valid-config-files/ignore-patterns-only-2.txt"));
  checkRootNode(ignore_2, BPOL_none, 0, 0, false, 4, 0);
  assert_true(checkIgnoreExpression(ignore_2, "foo", 7));
  assert_true(checkIgnoreExpression(ignore_2, "bar", 9));
  assert_true(checkIgnoreExpression(ignore_2, "foo-bar", 12));
  assert_true(checkIgnoreExpression(ignore_2, ".*\\.png", 17));

  const SearchNode *summarize_1 = searchTreeLoad(str("valid-config-files/summarize-patterns.txt"));
  checkRootNode(summarize_1, BPOL_none, 0, 0, false, 0, 3);
  assert_true(checkSummarizeExpression(summarize_1, "\\.git$", 3));
  assert_true(checkSummarizeExpression(summarize_1, "^/home/user/\\.cache$", 13));
  assert_true(checkSummarizeExpression(summarize_1, "^/home/user/\\.mozilla$", 14));

  const SearchNode *summarize_2 = searchTreeLoad(str("valid-config-files/summarize-patterns-mixed.txt"));
  checkRootNode(summarize_2, BPOL_none, 0, 2, false, 1, 2);
  assert_true(checkSummarizeExpression(summarize_2, "\\.cache$", 5));
  assert_true(checkSummarizeExpression(summarize_2, "\\.git$", 11));

  testIgnoringComments();
  testGroupEnd();

  testGroupStart("BOM and EOL variations");
  testSimpleConfigFile(str("valid-config-files/simple.txt"));
  testSimpleConfigFile(str("valid-config-files/simple-BOM.txt"));
  testSimpleConfigFile(str("valid-config-files/simple-noeol.txt"));
  testSimpleConfigFile(str("valid-config-files/simple-BOM-noeol.txt"));
  testGroupEnd();

  testGroupStart("broken config files");
  testBrokenConfigFiles();
  testGroupEnd();

  testGroupStart("config files containing null-bytes");
  testNullBytesConfigFiles();
  testGroupEnd();
}
