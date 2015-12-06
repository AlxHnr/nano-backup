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

/** @file
  Tests the filesystem search implementation.
*/

#include "search.h"

#include "test.h"
#include "test-common.h"
#include "string-table.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** Performs some checks on the given SearchResult.

  @param result A valid search result, which must have the type
  SRT_regular, SRT_symlink, SRT_directory or SRT_other.
*/
static void checkSearchResult(SearchResult result)
{
  switch(result.type)
  {
    case SRT_regular:
      assert_true(S_ISREG(result.stats.st_mode));
      break;
    case SRT_symlink:
      assert_true(S_ISLNK(result.stats.st_mode));
      break;
    case SRT_directory:
      assert_true(S_ISDIR(result.stats.st_mode));
      break;
    case SRT_other:
      break;
    default:
      die("unexpected search result type: %i", result.type);
  }

  assert_true(result.path.str[result.path.length] == '\0');
}

/** Skips all search results in the given context, which belong to the
  given path. It will terminate the program with failure if any error was
  encountered.

  @param context The context that should be fast-forwarded to the given
  path.
  @param cwd The path which should be skipped.

  @return The recursion depth count for unwinding and leaving the
  directories which lead to the given cwd.
*/
static size_t skipCwd(SearchContext *context, String cwd)
{
  size_t recursion_depth = 0;

  while(true)
  {
    SearchResult result = searchGetNext(context);

    if(result.type != SRT_directory)
    {
      die("failed to find \"%s\" in the given context", cwd.str);
    }

    checkSearchResult(result);
    if(strCompare(result.path, cwd))
    {
      break;
    }
    else if(result.policy != BPOL_none)
    {
      die("unexpected policy in \"%s\"", result.path.str);
    }

    recursion_depth++;
  }

  return recursion_depth;
}

/** Returns a copy of the given string without the given cwd. This function
  is unsafe and doesn't perform any checks.

  @param string The string that starts with the given cwd.
  @param cwd The cwd path.

  @return The copy of the trimmed string containing a null-terminated
  buffer.
*/
static String trimCwd(String string, String cwd)
{
  return strCopy(str(&string.str[cwd.length + 1]));
}

/** Checks all nodes in the given search tree to be correctly set and
  updated by the search.

  @param root_node The search tree which should be checked.
  @param cwd_depth The recursion depth of the current working directory.

  @return The parent node of the first directory inside the cwd, or NULL if
  the check failed.
*/
static SearchNode *checkCwdTree(SearchNode *root_node, size_t cwd_depth)
{
  if(root_node->subnodes == NULL)
  {
    return NULL;
  }

  SearchNode *node = root_node->subnodes;
  for(size_t counter = 0; counter < cwd_depth; counter++)
  {
    if(node->subnodes == NULL || node->subnodes->next != NULL ||
       node->search_match != SRT_directory)
    {
      return NULL;
    }

    node = node->subnodes;
  }

  return node;
}

/** Finishes the search for the given context by leaving all the
  directories which lead to the current working directory. Counterpart to
  skipCwd().

  @param context The context which should be finished.
  @param recursion_depth The recursion depth required to reach the current
  working directory.
*/
static void finishSearch(SearchContext *context, size_t recursion_depth)
{
  /* Volatile is only required to suppress GCC's warnings about longjumps
     inside the assert statement. */
  for(volatile size_t counter = 0; counter < recursion_depth; counter++)
  {
    SearchResult result = searchGetNext(context);
    assert_true(result.type == SRT_end_of_directory);
  }

  assert_true(searchGetNext(context).type == SRT_end_of_search);
}

/** Performs a search with the given context until its current directory
  has reached its end and stores the paths in the given StringTable.

  @param context The context used for searching.
  @param table The table, in which the found paths will be mapped to their
  policy. The paths will only contain the part after the given cwd and the
  policy will be incremented by 1.
  @param cwd A string used for trimming the strings that will be mapped in
  the given StringTable.

  @return The amount of files found during search.
*/
static size_t populateDirectoryTable(SearchContext *context,
                                     StringTable *table, String cwd)
{
  size_t file_count = 0;
  size_t recursion_depth = 1;
  while(recursion_depth > 0)
  {
    SearchResult result = searchGetNext(context);
    if(result.type == SRT_end_of_directory)
    {
      recursion_depth--;
    }
    else if(result.type == SRT_end_of_search)
    {
      die("reached end of search while populating string table");
    }
    else
    {
      checkSearchResult(result);
      String relative_path = trimCwd(result.path, cwd);
      if(strtableGet(table, relative_path) != NULL)
      {
        die("path \"%s\" was found twice during search",
            relative_path.str);
      }

      file_count += (result.type == SRT_regular ||
                     result.type == SRT_symlink);
      recursion_depth += result.type == SRT_directory;
      strtableMap(table, relative_path,
                  (void *)((size_t)result.policy + 1));
    }
  }

  return file_count;
}

/** Asserts that the given table contains a mapping of the given path to
  the given policy plus 1. */
static void checkHasPolicy(StringTable *table, const char *path,
                           BackupPolicy policy)
{
  void *address = strtableGet(table, str(path));

  if(address == NULL)
  {
    die("\"%s\" with policy %i does not exist in the given table",
        path, policy);
  }

  assert_true(address == (void *)((size_t)policy + 1));
}

/** Asserts that various test data directories where ignored properly.

  @param table The table which contains all files found during search.
*/
static void checkHasIgnoredProperly(StringTable *table)
{
  assert_true(strtableGet(table, str("valid-config-files"))        == NULL);
  assert_true(strtableGet(table, str("broken-config-files"))       == NULL);
  assert_true(strtableGet(table, str("template-config-files"))     == NULL);
  assert_true(strtableGet(table, str("generated-config-files"))    == NULL);
  assert_true(strtableGet(table, str("generated-broken-metadata")) == NULL);
  assert_true(strtableGet(table, str("dummy-metadata"))            == NULL);
  assert_true(strtableGet(table, str("tmp"))                       == NULL);
}

/** Asserts that a subnode with the given properties exists or terminate
  the program with an error message.

  @param parent_node The parent node which subnode should be checked.
  @param name_str The name of the node that must exist.
  @param search_match The SearchResultType of the node that must exist.

  @return The node with the given properties.
*/
static SearchNode *checkSubnode(SearchNode *parent_node,
                                const char *name_str,
                                SearchResultType search_match)
{
  String name = str(name_str);
  for(SearchNode *node = parent_node->subnodes;
      node != NULL; node = node->next)
  {
    if(strCompare(node->name, name) && node->search_match == search_match)
    {
      return node;
    }
  }

  die("subnode couldn't be found: \"%s\"", name_str);
  return NULL;
}

/** Asserts that the given ignore expression exists in the given node with
  the specified match status.
*/
static void checkIgnoreExpression(SearchNode *node, const char *expression,
                                  bool has_matched)
{
  String name = str(expression);
  for(RegexList *element = *node->ignore_expressions;
      element != NULL; element = element->next)
  {
    if(strCompare(element->expression, name) &&
       element->has_matched == has_matched)
    {
      return;
    }
  }

  die("failed to find %smatched ignore expression \"%s\"",
      has_matched? "" : "un", expression);
}

/** Tests a search by using the generated config "simple-search.txt".

  @param cwd The path to the current working directory.
*/
static void testSimpleSearch(String cwd)
{
  SearchNode *root = searchTreeLoad("generated-config-files/simple-search.txt");
  SearchContext *context = searchNew(root);
  assert_true(context != NULL);

  volatile size_t cwd_depth = skipCwd(context, cwd);
  StringTable *found_files = strtableNew();
  assert_true(populateDirectoryTable(context, found_files, cwd) == 29);
  finishSearch(context, cwd_depth);

  checkHasPolicy(found_files, "empty.txt",   BPOL_track);
  checkHasPolicy(found_files, "example.txt", BPOL_track);
  checkHasPolicy(found_files, "symlink.txt", BPOL_mirror);
  checkHasIgnoredProperly(found_files);

  assert_true(strtableGet(found_files, str("non-existing-directory")) == NULL);
  assert_true(strtableGet(found_files, str("test directory/non-existing-file.txt")) == NULL);
  assert_true(strtableGet(found_files, str("test directory/non-existing-regex")) == NULL);

  checkHasPolicy(found_files, "test directory",                            BPOL_copy);
  checkHasPolicy(found_files, "test directory/.empty",                     BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden",                    BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden",            BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-A.txt", BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-B.txt", BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-C.txt", BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/test file.☢",        BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/❤❤❤.txt",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden 1",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden 2",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden 3",                  BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden symlink",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/bar-a.txt",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/bar-b.txt",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/empty-directory",            BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1",                      BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1/bar",                  BPOL_track);
  checkHasPolicy(found_files, "test directory/foo 1/bar/1.txt",            BPOL_track);
  checkHasPolicy(found_files, "test directory/foo 1/bar/2.txt",            BPOL_track);
  checkHasPolicy(found_files, "test directory/foo 1/bar/3.txt",            BPOL_track);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-a.txt",      BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-b.txt",      BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-c.txt",      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1/♞.☂",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/foobar a1.txt",              BPOL_copy);
  checkHasPolicy(found_files, "test directory/foobar a2.txt",              BPOL_copy);
  checkHasPolicy(found_files, "test directory/foobar b1.txt",              BPOL_copy);
  checkHasPolicy(found_files, "test directory/foobar b2.txt",              BPOL_copy);
  checkHasPolicy(found_files, "test directory/symlink",                    BPOL_mirror);
  checkHasPolicy(found_files, "test directory/φ.txt",                      BPOL_copy);
  checkHasPolicy(found_files, "test directory/€.txt",                      BPOL_copy);
  strtableFree(found_files);

  SearchNode *node = checkCwdTree(root, cwd_depth);
  assert_true(node != NULL);

  checkSubnode(node, "non-existing-directory", SRT_none);
  checkSubnode(node, "^e.*\\.txt$", SRT_regular);
  checkSubnode(node, "symlink.txt", SRT_symlink);

  SearchNode *test_dir = checkSubnode(node, "test directory", SRT_directory);
  checkSubnode(test_dir, "non-existing-file.txt", SRT_none);
  checkSubnode(test_dir, "^non-existing-regex$",  SRT_none);
  checkSubnode(test_dir, ".empty",                SRT_directory);
  checkSubnode(test_dir, " 3$",                   SRT_regular);
  checkSubnode(test_dir, "symlink",               SRT_symlink);

  SearchNode *hidden = checkSubnode(test_dir, ".hidden", SRT_directory);
  checkSubnode(hidden, ".hidden", SRT_directory);
  checkSubnode(hidden, "\\.txt$", SRT_regular);

  SearchNode *foo_1 = checkSubnode(test_dir, "foo 1", SRT_directory);
  checkSubnode(foo_1, "bar",             SRT_directory);
  checkSubnode(foo_1, "test-file-c.txt", SRT_regular);
}

/** Tests a search by using the generated config "ignore-expressions.txt".

  @param cwd The path to the current working directory.
*/
static void testIgnoreExpressions(String cwd)
{
  SearchNode *root = searchTreeLoad("generated-config-files/ignore-expressions.txt");
  SearchContext *context = searchNew(root);
  assert_true(context != NULL);

  volatile size_t cwd_depth = skipCwd(context, cwd);
  StringTable *found_files = strtableNew();
  assert_true(populateDirectoryTable(context, found_files, cwd) == 19);
  finishSearch(context, cwd_depth);

  checkIgnoreExpression(root, "test/data/.*(tmp|config-files|metadata)$", true);
  checkIgnoreExpression(root, "test/data/e.+\\.txt$",        true);
  checkIgnoreExpression(root, "^will-never-match-anything$", false);
  checkIgnoreExpression(root, "symlink",                     true);
  checkIgnoreExpression(root, "[b1]\\.txt$",                 true);
  checkIgnoreExpression(root, "bar-a\\.txt$",                false);
  checkIgnoreExpression(root, "€\\.txt$",                    true);
  checkIgnoreExpression(root, "^will-never-match-any-file$", false);
  checkIgnoreExpression(root, "directory$",                  true);

  assert_true(strtableGet(found_files, str("empty.txt"))   == NULL);
  assert_true(strtableGet(found_files, str("example.txt")) == NULL);
  checkHasPolicy(found_files, "symlink.txt", BPOL_mirror);
  checkHasIgnoredProperly(found_files);

  checkHasPolicy(found_files, "test directory",                            BPOL_copy);
  checkHasPolicy(found_files, "test directory/.empty",                     BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden",                    BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden",            BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-A.txt", BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-B.txt", BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-C.txt", BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/test file.☢",        BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden/❤❤❤.txt",            BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden 1",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden 2",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden 3",                  BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden symlink",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/bar-a.txt",                  BPOL_track);
  assert_true(strtableGet(found_files, str("test directory/bar-b.txt"))       == NULL);
  assert_true(strtableGet(found_files, str("test directory/empty-directory")) == NULL);
  checkHasPolicy(found_files, "test directory/foo 1",                      BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1/bar",                  BPOL_copy);
  assert_true(strtableGet(found_files, str("test directory/foo 1/bar/1.txt")) == NULL);
  checkHasPolicy(found_files, "test directory/foo 1/bar/2.txt",            BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1/bar/3.txt",            BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-a.txt",      BPOL_copy);
  assert_true(strtableGet(found_files, str("test directory/foo 1/test-file-b.txt")) == NULL);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-c.txt",      BPOL_copy);
  checkHasPolicy(found_files, "test directory/foo 1/♞.☂",                  BPOL_copy);
  assert_true(strtableGet(found_files, str("test directory/foobar a1.txt")) == NULL);
  checkHasPolicy(found_files, "test directory/foobar a2.txt",              BPOL_copy);
  assert_true(strtableGet(found_files, str("test directory/foobar b1.txt")) == NULL);
  checkHasPolicy(found_files, "test directory/foobar b2.txt",              BPOL_copy);
  assert_true(strtableGet(found_files, str("test directory/symlink")) == NULL);
  checkHasPolicy(found_files, "test directory/φ.txt",                      BPOL_copy);
  assert_true(strtableGet(found_files, str("test directory/€.txt")) == NULL);
  strtableFree(found_files);

  SearchNode *node = checkCwdTree(root, cwd_depth);
  assert_true(node != NULL);

  checkSubnode(node, "symlink", SRT_symlink);

  SearchNode *test_dir = checkSubnode(node, "test directory", SRT_directory);
  checkSubnode(test_dir, ".hidden symlink", SRT_symlink);
  checkSubnode(test_dir, "^bar-a\\.txt$", SRT_regular);
}

/** Tests a search by using the generated config "symlink-following.txt".

  @param cwd The path to the current working directory.
*/
static void testSymlinkFollowing(String cwd)
{
  SearchNode *root = searchTreeLoad("generated-config-files/symlink-following.txt");
  SearchContext *context = searchNew(root);
  assert_true(context != NULL);

  volatile size_t cwd_depth = skipCwd(context, cwd);
  StringTable *found_files = strtableNew();
  assert_true(populateDirectoryTable(context, found_files, cwd) == 20);
  finishSearch(context, cwd_depth);

  checkIgnoreExpression(root, "test/data/[^/]+$", true);
  checkIgnoreExpression(root, "foo 1$", true);

  assert_true(strtableGet(found_files, str("empty.txt"))   == NULL);
  assert_true(strtableGet(found_files, str("example.txt")) == NULL);
  assert_true(strtableGet(found_files, str("symlink.txt")) == NULL);
  checkHasIgnoredProperly(found_files);

  checkHasPolicy(found_files, "test directory",                            BPOL_track);
  checkHasPolicy(found_files, "test directory/.empty",                     BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden",                    BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden",            BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-A.txt", BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-B.txt", BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-C.txt", BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/test file.☢",        BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden/❤❤❤.txt",            BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden 1",                  BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden 2",                  BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden 3",                  BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden symlink",            BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden symlink/1.txt",      BPOL_track);
  checkHasPolicy(found_files, "test directory/.hidden symlink/2.txt",      BPOL_copy);
  checkHasPolicy(found_files, "test directory/.hidden symlink/3.txt",      BPOL_track);
  checkHasPolicy(found_files, "test directory/bar-a.txt",                  BPOL_track);
  checkHasPolicy(found_files, "test directory/bar-b.txt",                  BPOL_track);
  checkHasPolicy(found_files, "test directory/empty-directory",            BPOL_track);
  assert_true(strtableGet(found_files, str("test directory/foo 1")) == NULL);
  checkHasPolicy(found_files, "test directory/foobar a1.txt",              BPOL_track);
  checkHasPolicy(found_files, "test directory/foobar a2.txt",              BPOL_track);
  checkHasPolicy(found_files, "test directory/foobar b1.txt",              BPOL_track);
  checkHasPolicy(found_files, "test directory/foobar b2.txt",              BPOL_track);
  checkHasPolicy(found_files, "test directory/symlink",                    BPOL_track);
  checkHasPolicy(found_files, "test directory/φ.txt",                      BPOL_track);
  checkHasPolicy(found_files, "test directory/€.txt",                      BPOL_track);
  strtableFree(found_files);

  SearchNode *node = checkCwdTree(root, cwd_depth);
  assert_true(node != NULL);

  SearchNode *test_dir = checkSubnode(node, "test directory", SRT_directory);

  SearchNode *hidden_symlink = checkSubnode(test_dir, ".hidden symlink", SRT_directory);
  checkSubnode(hidden_symlink, "2.txt", SRT_regular);

  SearchNode *empty_dir = checkSubnode(test_dir, "empty-directory", SRT_directory);
  checkSubnode(empty_dir, ".*", SRT_none);
}

/** Performs a search using the generated config file
  "mismatched-paths.txt" and asserts that the search results behave like
  expected.

  @param cwd The path to the current working directory.
*/
static void testMismatchedPaths(String cwd)
{
  SearchNode *root = searchTreeLoad("generated-config-files/mismatched-paths.txt");
  SearchContext *context = searchNew(root);
  assert_true(context != NULL);

  volatile size_t cwd_depth = skipCwd(context, cwd);
  StringTable *found_files = strtableNew();
  assert_true(populateDirectoryTable(context, found_files, cwd) == 2);
  finishSearch(context, cwd_depth);

  checkHasPolicy(found_files, "empty.txt", BPOL_none);
  assert_true(strtableGet(found_files, str("empty.txt/file 1.txt")) == NULL);

  checkHasPolicy(found_files, "symlink.txt", BPOL_none);
  assert_true(strtableGet(found_files, str("symlink.txt/foo-bar.txt")) == NULL);

  assert_true(strtableGet(found_files, str("example.txt"))            == NULL);
  checkHasIgnoredProperly(found_files);

  checkHasPolicy(found_files, "test directory", BPOL_none);
  assert_true(strtableGet(found_files, str("test directory/super-file.txt"))  == NULL);
  assert_true(strtableGet(found_files, str("test directory/.empty"))          == NULL);
  assert_true(strtableGet(found_files, str("test directory/.hidden"))         == NULL);
  assert_true(strtableGet(found_files, str("test directory/.hidden 1"))       == NULL);
  assert_true(strtableGet(found_files, str("test directory/.hidden 2"))       == NULL);
  assert_true(strtableGet(found_files, str("test directory/.hidden 3"))       == NULL);
  assert_true(strtableGet(found_files, str("test directory/.hidden symlink")) == NULL);
  assert_true(strtableGet(found_files, str("test directory/bar-a.txt"))       == NULL);
  assert_true(strtableGet(found_files, str("test directory/bar-b.txt"))       == NULL);
  assert_true(strtableGet(found_files, str("test directory/empty-directory")) == NULL);
  assert_true(strtableGet(found_files, str("test directory/foo 1"))           == NULL);
  assert_true(strtableGet(found_files, str("test directory/foobar a1.txt"))   == NULL);
  assert_true(strtableGet(found_files, str("test directory/foobar a2.txt"))   == NULL);
  assert_true(strtableGet(found_files, str("test directory/foobar b1.txt"))   == NULL);
  assert_true(strtableGet(found_files, str("test directory/foobar b2.txt"))   == NULL);
  assert_true(strtableGet(found_files, str("test directory/symlink"))         == NULL);
  assert_true(strtableGet(found_files, str("test directory/φ.txt"))           == NULL);
  assert_true(strtableGet(found_files, str("test directory/€.txt"))           == NULL);
  strtableFree(found_files);

  SearchNode *node = checkCwdTree(root, cwd_depth);
  assert_true(node != NULL);

  SearchNode *empty_txt = checkSubnode(node, "empty.txt", SRT_regular);
  checkSubnode(empty_txt, "file 1.txt", SRT_none);

  SearchNode *symlink = checkSubnode(node, "symlink.txt", SRT_regular);
  checkSubnode(symlink, "foo-bar.txt", SRT_none);

  SearchNode *test_dir = checkSubnode(node, "test directory", SRT_directory);
  checkSubnode(test_dir, "super-file.txt", SRT_none);
}

/** Performs a search by using the generated config file
  "complex-search.txt" and asserts that the search behaves like expected.

  @param cwd The full path to the current working directory.
*/
static void testComplexSearch(String cwd)
{
  SearchNode *root = searchTreeLoad("generated-config-files/complex-search.txt");
  SearchContext *context = searchNew(root);
  assert_true(context != NULL);

  volatile size_t cwd_depth = skipCwd(context, cwd);
  StringTable *found_files = strtableNew();
  assert_true(populateDirectoryTable(context, found_files, cwd) == 26);
  finishSearch(context, cwd_depth);

  checkIgnoreExpression(root, "test/data/.*(tmp|config-files|metadata)$", true);
  checkIgnoreExpression(root, "^never-matches-anything$",   false);
  checkIgnoreExpression(root, "\\.hidden symlink/2\\.txt$", false);
  checkIgnoreExpression(root, "1\\.txt$",                   true);
  checkIgnoreExpression(root, "foobar",                     true);

  checkHasPolicy(found_files, "empty.txt",   BPOL_copy);
  checkHasPolicy(found_files, "example.txt", BPOL_copy);
  checkHasPolicy(found_files, "symlink.txt", BPOL_copy);
  checkHasIgnoredProperly(found_files);

  checkHasPolicy(found_files, "test directory",                            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.empty",                     BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden",                    BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-A.txt", BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-B.txt", BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden/.hidden/test-C.txt", BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden/test file.☢",        BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden/❤❤❤.txt",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden 1",                  BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden 2",                  BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden 3",                  BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden symlink",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden symlink/1.txt",      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden symlink/2.txt",      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/.hidden symlink/3.txt",      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/bar-a.txt",                  BPOL_mirror);
  checkHasPolicy(found_files, "test directory/bar-b.txt",                  BPOL_mirror);
  checkHasPolicy(found_files, "test directory/empty-directory",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1",                      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1/bar",                  BPOL_mirror);
  assert_true(strtableGet(found_files, str("test directory/foo 1/bar/1.txt")) == NULL);
  checkHasPolicy(found_files, "test directory/foo 1/bar/2.txt",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1/bar/3.txt",            BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-a.txt",      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-b.txt",      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1/test-file-c.txt",      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/foo 1/♞.☂",                  BPOL_mirror);
  assert_true(strtableGet(found_files, str("test directory/foobar a1.txt")) == NULL);
  assert_true(strtableGet(found_files, str("test directory/foobar a2.txt")) == NULL);
  assert_true(strtableGet(found_files, str("test directory/foobar b1.txt")) == NULL);
  assert_true(strtableGet(found_files, str("test directory/foobar b2.txt")) == NULL);
  checkHasPolicy(found_files, "test directory/symlink",                    BPOL_mirror);
  checkHasPolicy(found_files, "test directory/φ.txt",                      BPOL_mirror);
  checkHasPolicy(found_files, "test directory/€.txt",                      BPOL_mirror);
  strtableFree(found_files);

  SearchNode *node = checkCwdTree(root, cwd_depth);
  assert_true(node != NULL);

  checkSubnode(node, "^[es]", SRT_regular);

  SearchNode *test_dir = checkSubnode(node, "^tes", SRT_directory);

  SearchNode *symlink = checkSubnode(test_dir, " symlink", SRT_directory);
  checkSubnode(symlink, ".*", SRT_regular);

  SearchNode *hidden = checkSubnode(test_dir, "^.hidden [1-3]$", SRT_regular);
  checkSubnode(hidden, ".*", SRT_none);
  checkSubnode(hidden, "2.txt", SRT_none);
}

int main(void)
{
  testGroupStart("simple file search");
  String cwd = getCwd();
  testSimpleSearch(cwd);
  testGroupEnd();

  testGroupStart("ignore expressions");
  testIgnoreExpressions(cwd);
  testGroupEnd();

  testGroupStart("symlink following rules");
  testSymlinkFollowing(cwd);
  testGroupEnd();

  testGroupStart("mismatched paths");
  testMismatchedPaths(cwd);
  testGroupEnd();

  testGroupStart("complex file search");
  testComplexSearch(cwd);
  testGroupEnd();
}
