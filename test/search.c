#include "search.h"

#include "error-handling.h"
#include "memory-pool.h"
#include "safe-wrappers.h"
#include "string-table.h"
#include "test-common.h"
#include "test.h"

typedef struct
{
  BackupPolicy policy;

  /** The node which matched the path, or NULL. */
  const SearchNode *node;
} FoundPathInfo;

/** Performs some checks on the given SearchResult.

  @param result A valid search result, which must have the type
  SRT_regular, SRT_symlink, SRT_directory or SRT_other.
*/
static void checkSearchResult(const SearchResult result)
{
  switch(result.type)
  {
    case SRT_regular_file: assert_true(S_ISREG(result.stats.st_mode)); break;
    case SRT_symlink: assert_true(S_ISLNK(result.stats.st_mode)); break;
    case SRT_directory: assert_true(S_ISDIR(result.stats.st_mode)); break;
    case SRT_other: break;
    default: die("unexpected search result type: %u", result.type);
  }
}

/** Skips all search results in the given iterator, which belong to the
  given path. It will terminate the program with failure if any error was
  encountered.

  @param iterator The iterator that should be fast-forwarded to the given
  path.
  @param cwd The path which should be skipped.
  @param root_node The root of the search tree.

  @return The recursion depth count for unwinding and leaving the
  directories which lead to the given cwd.
*/
static size_t skipCwd(SearchIterator *iterator, StringView cwd, const SearchNode *root_node)
{
  size_t recursion_depth = 0;
  const SearchNode *node = root_node->subnodes;

  while(true)
  {
    const SearchResult result = searchGetNext(iterator);

    if(result.type != SRT_directory)
    {
      die("failed to find \"" PRI_STR "\" in the given iterator", STR_FMT(cwd));
    }
    else if(result.node == NULL || result.node != node)
    {
      die("search result contains invalid node for path \"" PRI_STR "\"", STR_FMT(result.path));
    }

    checkSearchResult(result);
    if(strEqual(result.path, cwd))
    {
      break;
    }
    if(result.policy != BPOL_none)
    {
      die("unexpected policy in \"" PRI_STR "\"", STR_FMT(result.path));
    }
    else
    {
      node = node->subnodes;
      recursion_depth++;
    }
  }

  return recursion_depth;
}

/** Returns a copy of the given string without the given cwd. This function
  is unsafe and doesn't perform any checks.

  @param string The string that starts with the given cwd.
  @param cwd The cwd path.

  @return The copy of the trimmed string.
*/
static StringView trimCwd(StringView string, StringView cwd)
{
  return strLegacyCopy(strUnterminated(&string.content[cwd.length + 1], string.length - 1 - cwd.length));
}

/** Asserts that all nodes in the given search tree got correctly set and
  updated by the search.

  @param root_node The search tree to check.
  @param cwd_depth The recursion depth of the current working directory.

  @return The parent node of the first directory inside the cwd.
*/
static const SearchNode *checkCwdTree(const SearchNode *root_node, const size_t cwd_depth)
{
  if(root_node->subnodes == NULL)
  {
    die("root node doesn't have subnodes");
  }

  const SearchNode *node = root_node->subnodes;
  for(size_t counter = 0; counter < cwd_depth; counter++)
  {
    if(node->subnodes == NULL)
    {
      die("node doesn't have subnodes: \"" PRI_STR "\"", STR_FMT(node->name));
    }
    else if(node->subnodes->next != NULL)
    {
      die("node has too many subnodes: \"" PRI_STR "\"", STR_FMT(node->name));
    }
    else if(node->search_match != SRT_directory)
    {
      die("node has not matched a directory: \"" PRI_STR "\"", STR_FMT(node->name));
    }

    node = node->subnodes;
  }

  return node;
}

/** Finishes the search for the given iterator by leaving all the
  directories which lead to the current working directory. Counterpart to
  skipCwd().

  @param iterator The iterator which should be finished.
  @param recursion_depth The recursion depth required to reach the current
  working directory.
*/
static void finishSearch(SearchIterator *iterator, const size_t recursion_depth)
{
  for(size_t counter = 0; counter < recursion_depth; counter++)
  {
    SearchResult result = searchGetNext(iterator);
    assert_true(result.type == SRT_end_of_directory);
  }

  assert_true(searchGetNext(iterator).type == SRT_end_of_search);
}

/** Performs a search with the given iterator until its current directory
  has reached its end and stores the paths in the given StringTable.

  @param iterator The iterator used for searching.
  @param table The table, in which the found paths will be mapped to their
  policy. The paths will only contain the part after the given cwd and the
  policy will be incremented by 1.
  @param cwd A string used for trimming the strings that will be mapped in
  the given StringTable.

  @return The amount of files found during search.
*/
static size_t populateDirectoryTable(SearchIterator *iterator, StringTable *table, StringView cwd)
{
  size_t file_count = 0;
  size_t recursion_depth = 1;
  while(recursion_depth > 0)
  {
    const SearchResult result = searchGetNext(iterator);
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
      StringView relative_path = trimCwd(result.path, cwd);
      if(strTableGet(table, relative_path) != NULL)
      {
        die("path \"" PRI_STR "\" was found twice during search", STR_FMT(relative_path));
      }

      file_count += (result.type == SRT_regular_file || result.type == SRT_symlink);
      recursion_depth += result.type == SRT_directory;

      FoundPathInfo *info = mpAlloc(sizeof *info);
      info->policy = result.policy;
      info->node = result.node;
      strTableMap(table, relative_path, info);
    }
  }

  return file_count;
}

/** Asserts that the given path was found with the specified properties.

  @param table The table containing found paths.
  @param path The path which should be checked.
  @param policy The policy of the found path.
  @param node The node trough which the path was found, or NULL.
*/
static void checkFoundPath(const StringTable *table, const char *path, const BackupPolicy policy,
                           const SearchNode *node)
{
  const FoundPathInfo *info = strTableGet(table, str(path));

  if(info == NULL)
  {
    die("path was not found during search: \"%s\"", path);
  }
  else if(info->policy != policy)
  {
    die("path was found with the wrong policy: \"%s\"", path);
  }
  else if(info->node != node)
  {
    die("path was found trough the wrong node: \"%s\"", path);
  }
}

/** Asserts that various test data directories where ignored properly.

  @param table The table which contains all files found during search.
*/
static void checkHasIgnoredProperly(const StringTable *table)
{
  assert_true(strTableGet(table, str("valid-config-files")) == NULL);
  assert_true(strTableGet(table, str("broken-config-files")) == NULL);
  assert_true(strTableGet(table, str("template-config-files")) == NULL);
  assert_true(strTableGet(table, str("generated-config-files")) == NULL);
  assert_true(strTableGet(table, str("tmp")) == NULL);
}

/** Asserts that a subnode with the given properties exists or terminate
  the program with an error message.

  @param parent_node The parent node which subnode should be checked.
  @param name_str The name of the node that must exist.
  @param search_match The SearchResultType of the node that must exist.

  @return The node with the given properties.
*/
static const SearchNode *findSubnode(const SearchNode *parent_node, const char *name_str,
                                     const SearchResultType search_match)
{
  StringView name = str(name_str);
  for(const SearchNode *node = parent_node->subnodes; node != NULL; node = node->next)
  {
    if(strEqual(node->name, name) && node->search_match == search_match)
    {
      return node;
    }
  }

  die("subnode couldn't be found: \"%s\"", name_str);
  return NULL;
}

static void checkIgnoreExpression(const SearchNode *node, const char *expression, const bool has_matched)
{
  StringView name = str(expression);
  for(const RegexList *element = *node->ignore_expressions; element != NULL; element = element->next)
  {
    if(strEqual(element->expression, name) && element->has_matched == has_matched)
    {
      return;
    }
  }

  die("failed to find %smatched ignore expression \"%s\"", has_matched ? "" : "un", expression);
}

/** Tests a search by using the generated config "simple-search.txt".

  @param cwd The path to the current working directory.
*/
static void testSimpleSearch(StringView cwd)
{
  SearchNode *root = searchTreeLoad(str("generated-config-files/simple-search.txt"));
  SearchIterator *iterator = searchNew(root);
  assert_true(iterator != NULL);

  const size_t cwd_depth = skipCwd(iterator, cwd, root);
  CR_Region *paths_region = CR_RegionNew();
  StringTable *paths = strTableNew(paths_region);
  assert_true(populateDirectoryTable(iterator, paths, cwd) == 29);
  finishSearch(iterator, cwd_depth);

  /* Check nodes in search tree. */
  const SearchNode *n_cwd = checkCwdTree(root, cwd_depth);
  const SearchNode *n_e_txt = findSubnode(n_cwd, "^e.*\\.txt$", SRT_regular_file);
  const SearchNode *n_symlink_txt = findSubnode(n_cwd, "symlink.txt", SRT_symlink);
  findSubnode(n_cwd, "non-existing-directory", SRT_none);

  const SearchNode *n_test_dir = findSubnode(n_cwd, "test directory", SRT_directory);
  const SearchNode *n_empty = findSubnode(n_test_dir, ".empty", SRT_directory);
  const SearchNode *n_3 = findSubnode(n_test_dir, " 3$", SRT_regular_file);
  const SearchNode *n_symlink = findSubnode(n_test_dir, "symlink", SRT_symlink);
  findSubnode(n_test_dir, "non-existing-file.txt", SRT_none);
  findSubnode(n_test_dir, "^non-existing-regex$", SRT_none);

  const SearchNode *n_hidden = findSubnode(n_test_dir, ".hidden", SRT_directory);
  const SearchNode *n_hidden_hidden = findSubnode(n_hidden, ".hidden", SRT_directory);
  const SearchNode *n_txt = findSubnode(n_hidden, "\\.txt$", SRT_regular_file);

  const SearchNode *n_foo_1 = findSubnode(n_test_dir, "foo 1", SRT_directory);
  const SearchNode *n_bar = findSubnode(n_foo_1, "bar", SRT_directory);
  const SearchNode *n_test_file_c = findSubnode(n_foo_1, "test-file-c.txt", SRT_regular_file);

  /* Check found paths. */
  checkHasIgnoredProperly(paths);
  assert_true(strTableGet(paths, str("non-existing-directory")) == NULL);
  assert_true(strTableGet(paths, str("test directory/non-existing-file.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/non-existing-regex")) == NULL);

  checkFoundPath(paths, "empty.txt", BPOL_track, n_e_txt);
  checkFoundPath(paths, "example.txt", BPOL_track, n_e_txt);
  checkFoundPath(paths, "symlink.txt", BPOL_mirror, n_symlink_txt);
  checkFoundPath(paths, "test directory", BPOL_copy, n_test_dir);
  checkFoundPath(paths, "test directory/.empty", BPOL_mirror, n_empty);
  checkFoundPath(paths, "test directory/.hidden", BPOL_copy, n_hidden);
  checkFoundPath(paths, "test directory/.hidden/.hidden", BPOL_track, n_hidden_hidden);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-A.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-B.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-C.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/test file.☢", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden/❤❤❤.txt", BPOL_mirror, n_txt);
  checkFoundPath(paths, "test directory/.hidden 1", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden 2", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden 3", BPOL_track, n_3);
  checkFoundPath(paths, "test directory/.hidden symlink", BPOL_mirror, n_symlink);
  checkFoundPath(paths, "test directory/bar-a.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/bar-b.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/empty-directory", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foo 1", BPOL_copy, n_foo_1);
  checkFoundPath(paths, "test directory/foo 1/bar", BPOL_track, n_bar);
  checkFoundPath(paths, "test directory/foo 1/bar/1.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/foo 1/bar/2.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/foo 1/bar/3.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-a.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-b.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-c.txt", BPOL_mirror, n_test_file_c);
  checkFoundPath(paths, "test directory/foo 1/♞.☂", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foobar a1.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foobar a2.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foobar b1.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foobar b2.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/symlink", BPOL_mirror, n_symlink);
  checkFoundPath(paths, "test directory/φ.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/€.txt", BPOL_copy, NULL);
  CR_RegionRelease(paths_region);
}

/** Tests a search by using the generated config "ignore-expressions.txt".

  @param cwd The path to the current working directory.
*/
static void testIgnoreExpressions(StringView cwd)
{
  SearchNode *root = searchTreeLoad(str("generated-config-files/ignore-expressions.txt"));
  SearchIterator *iterator = searchNew(root);
  assert_true(iterator != NULL);

  const size_t cwd_depth = skipCwd(iterator, cwd, root);
  CR_Region *paths_region = CR_RegionNew();
  StringTable *paths = strTableNew(paths_region);
  assert_true(populateDirectoryTable(iterator, paths, cwd) == 19);
  finishSearch(iterator, cwd_depth);

  /* Check nodes in search tree. */
  const SearchNode *n_cwd = checkCwdTree(root, cwd_depth);
  const SearchNode *n_symlink = findSubnode(n_cwd, "symlink", SRT_symlink);
  const SearchNode *n_test_dir = findSubnode(n_cwd, "test directory", SRT_directory);
  const SearchNode *n_hidden_symlink = findSubnode(n_test_dir, ".hidden symlink", SRT_symlink);
  const SearchNode *n_bar_a_txt = findSubnode(n_test_dir, "^bar-a\\.txt$", SRT_regular_file);

  /* Check found paths. */
  checkHasIgnoredProperly(paths);
  assert_true(strTableGet(paths, str("empty.txt")) == NULL);
  assert_true(strTableGet(paths, str("example.txt")) == NULL);

  checkFoundPath(paths, "symlink.txt", BPOL_mirror, n_symlink);
  checkFoundPath(paths, "test directory", BPOL_copy, n_test_dir);
  checkFoundPath(paths, "test directory/.empty", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-A.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-B.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-C.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden/test file.☢", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden/❤❤❤.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden 1", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden 2", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden 3", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/.hidden symlink", BPOL_mirror, n_hidden_symlink);
  checkFoundPath(paths, "test directory/bar-a.txt", BPOL_track, n_bar_a_txt);
  assert_true(strTableGet(paths, str("test directory/bar-b.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/empty-directory")) == NULL);
  checkFoundPath(paths, "test directory/foo 1", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foo 1/bar", BPOL_copy, NULL);
  assert_true(strTableGet(paths, str("test directory/foo 1/bar/1.txt")) == NULL);
  checkFoundPath(paths, "test directory/foo 1/bar/2.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foo 1/bar/3.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-a.txt", BPOL_copy, NULL);
  assert_true(strTableGet(paths, str("test directory/foo 1/test-file-b.txt")) == NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-c.txt", BPOL_copy, NULL);
  checkFoundPath(paths, "test directory/foo 1/♞.☂", BPOL_copy, NULL);
  assert_true(strTableGet(paths, str("test directory/foobar a1.txt")) == NULL);
  checkFoundPath(paths, "test directory/foobar a2.txt", BPOL_copy, NULL);
  assert_true(strTableGet(paths, str("test directory/foobar b1.txt")) == NULL);
  checkFoundPath(paths, "test directory/foobar b2.txt", BPOL_copy, NULL);
  assert_true(strTableGet(paths, str("test directory/symlink")) == NULL);
  checkFoundPath(paths, "test directory/φ.txt", BPOL_copy, NULL);
  assert_true(strTableGet(paths, str("test directory/€.txt")) == NULL);
  CR_RegionRelease(paths_region);

  /* Check ignore expressions. */
  checkIgnoreExpression(root, "test/data/.*(tmp|config-files|metadata)$", true);
  checkIgnoreExpression(root, "test/data/e.+\\.txt$", true);
  checkIgnoreExpression(root, "^will-never-match-anything$", false);
  checkIgnoreExpression(root, "symlink", true);
  checkIgnoreExpression(root, "[b1]\\.txt$", true);
  checkIgnoreExpression(root, "bar-a\\.txt$", false);
  checkIgnoreExpression(root, "€\\.txt$", true);
  checkIgnoreExpression(root, "^will-never-match-any-file$", false);
  checkIgnoreExpression(root, "directory$", true);
}

/** Tests a search by using the generated config "symlink-following.txt".

  @param cwd The path to the current working directory.
*/
static void testSymlinkFollowing(StringView cwd)
{
  SearchNode *root = searchTreeLoad(str("generated-config-files/symlink-following.txt"));
  SearchIterator *iterator = searchNew(root);
  assert_true(iterator != NULL);

  const size_t cwd_depth = skipCwd(iterator, cwd, root);
  CR_Region *paths_region = CR_RegionNew();
  StringTable *paths = strTableNew(paths_region);
  assert_true(populateDirectoryTable(iterator, paths, cwd) == 20);
  finishSearch(iterator, cwd_depth);

  /* Check nodes in search tree. */
  const SearchNode *n_cwd = checkCwdTree(root, cwd_depth);
  const SearchNode *n_test_dir = findSubnode(n_cwd, "test directory", SRT_directory);

  const SearchNode *n_hidden_symlink = findSubnode(n_test_dir, ".hidden symlink", SRT_directory);
  const SearchNode *n_2_txt = findSubnode(n_hidden_symlink, "2.txt", SRT_regular_file);

  const SearchNode *n_empty_dir = findSubnode(n_test_dir, "empty-directory", SRT_directory);
  findSubnode(n_empty_dir, ".*", SRT_none);

  /* Check found paths. */
  checkHasIgnoredProperly(paths);
  assert_true(strTableGet(paths, str("empty.txt")) == NULL);
  assert_true(strTableGet(paths, str("example.txt")) == NULL);
  assert_true(strTableGet(paths, str("symlink.txt")) == NULL);

  checkFoundPath(paths, "test directory", BPOL_track, n_test_dir);
  checkFoundPath(paths, "test directory/.empty", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-A.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-B.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-C.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/test file.☢", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden/❤❤❤.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden 1", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden 2", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden 3", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden symlink", BPOL_track, n_hidden_symlink);
  checkFoundPath(paths, "test directory/.hidden symlink/1.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/.hidden symlink/2.txt", BPOL_copy, n_2_txt);
  checkFoundPath(paths, "test directory/.hidden symlink/3.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/bar-a.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/bar-b.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/empty-directory", BPOL_track, n_empty_dir);
  assert_true(strTableGet(paths, str("test directory/foo 1")) == NULL);
  checkFoundPath(paths, "test directory/foobar a1.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/foobar a2.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/foobar b1.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/foobar b2.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/symlink", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/φ.txt", BPOL_track, NULL);
  checkFoundPath(paths, "test directory/€.txt", BPOL_track, NULL);
  CR_RegionRelease(paths_region);

  /* Check ignore expressions. */
  checkIgnoreExpression(root, "test/data/[^/]+$", true);
  checkIgnoreExpression(root, "foo 1$", true);
}

/** Performs a search using the generated config file
  "mismatched-paths.txt" and asserts that the search results behave like
  expected.

  @param cwd The path to the current working directory.
*/
static void testMismatchedPaths(StringView cwd)
{
  SearchNode *root = searchTreeLoad(str("generated-config-files/mismatched-paths.txt"));
  SearchIterator *iterator = searchNew(root);
  assert_true(iterator != NULL);

  const size_t cwd_depth = skipCwd(iterator, cwd, root);
  CR_Region *paths_region = CR_RegionNew();
  StringTable *paths = strTableNew(paths_region);
  assert_true(populateDirectoryTable(iterator, paths, cwd) == 2);
  finishSearch(iterator, cwd_depth);

  /* Check nodes in search tree. */
  const SearchNode *n_cwd = checkCwdTree(root, cwd_depth);

  const SearchNode *n_empty_txt = findSubnode(n_cwd, "empty.txt", SRT_regular_file);
  findSubnode(n_empty_txt, "file 1.txt", SRT_none);

  const SearchNode *n_symlink_txt = findSubnode(n_cwd, "symlink.txt", SRT_regular_file);
  findSubnode(n_symlink_txt, "foo-bar.txt", SRT_none);

  const SearchNode *n_test_dir = findSubnode(n_cwd, "test directory", SRT_directory);
  findSubnode(n_test_dir, "super-file.txt", SRT_none);

  /* Check found paths. */
  checkHasIgnoredProperly(paths);
  assert_true(strTableGet(paths, str("example.txt")) == NULL);

  checkFoundPath(paths, "empty.txt", BPOL_none, n_empty_txt);
  assert_true(strTableGet(paths, str("empty.txt/file 1.txt")) == NULL);
  checkFoundPath(paths, "symlink.txt", BPOL_none, n_symlink_txt);
  assert_true(strTableGet(paths, str("symlink.txt/foo-bar.txt")) == NULL);
  checkFoundPath(paths, "test directory", BPOL_none, n_test_dir);
  assert_true(strTableGet(paths, str("test directory/super-file.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/.empty")) == NULL);
  assert_true(strTableGet(paths, str("test directory/.hidden")) == NULL);
  assert_true(strTableGet(paths, str("test directory/.hidden 1")) == NULL);
  assert_true(strTableGet(paths, str("test directory/.hidden 2")) == NULL);
  assert_true(strTableGet(paths, str("test directory/.hidden 3")) == NULL);
  assert_true(strTableGet(paths, str("test directory/.hidden symlink")) == NULL);
  assert_true(strTableGet(paths, str("test directory/bar-a.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/bar-b.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/empty-directory")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foo 1")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foobar a1.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foobar a2.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foobar b1.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foobar b2.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/symlink")) == NULL);
  assert_true(strTableGet(paths, str("test directory/φ.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/€.txt")) == NULL);
  CR_RegionRelease(paths_region);
}

/** Performs a search by using the generated config file
  "complex-search.txt" and asserts that the search behaves like expected.

  @param cwd The full path to the current working directory.
*/
static void testComplexSearch(StringView cwd)
{
  SearchNode *root = searchTreeLoad(str("generated-config-files/complex-search.txt"));
  SearchIterator *iterator = searchNew(root);
  assert_true(iterator != NULL);

  const size_t cwd_depth = skipCwd(iterator, cwd, root);
  CR_Region *paths_region = CR_RegionNew();
  StringTable *paths = strTableNew(paths_region);
  assert_true(populateDirectoryTable(iterator, paths, cwd) == 26);
  finishSearch(iterator, cwd_depth);

  /* Check nodes in search tree. */
  const SearchNode *n_cwd = checkCwdTree(root, cwd_depth);
  const SearchNode *n_es = findSubnode(n_cwd, "^[es]", SRT_regular_file | SRT_symlink);

  const SearchNode *n_test_dir = findSubnode(n_cwd, "^tes", SRT_directory);
  const SearchNode *n_symlink = findSubnode(n_test_dir, " symlink", SRT_directory);
  const SearchNode *n_star = findSubnode(n_symlink, ".*", SRT_regular_file);

  const SearchNode *n_hidden_123 = findSubnode(n_test_dir, "^.hidden [1-3]$", SRT_regular_file);
  findSubnode(n_hidden_123, "2.txt", SRT_none);
  findSubnode(n_hidden_123, ".*", SRT_none);

  /* Check found paths. */
  checkHasIgnoredProperly(paths);
  checkFoundPath(paths, "empty.txt", BPOL_copy, n_es);
  checkFoundPath(paths, "example.txt", BPOL_copy, n_es);
  checkFoundPath(paths, "symlink.txt", BPOL_copy, n_es);

  checkFoundPath(paths, "test directory", BPOL_mirror, n_test_dir);
  checkFoundPath(paths, "test directory/.empty", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-A.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-B.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden/.hidden/test-C.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden/test file.☢", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden/❤❤❤.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/.hidden 1", BPOL_mirror, n_hidden_123);
  checkFoundPath(paths, "test directory/.hidden 2", BPOL_mirror, n_hidden_123);
  checkFoundPath(paths, "test directory/.hidden 3", BPOL_mirror, n_hidden_123);
  checkFoundPath(paths, "test directory/.hidden symlink", BPOL_mirror, n_symlink);
  checkFoundPath(paths, "test directory/.hidden symlink/1.txt", BPOL_mirror, n_star);
  checkFoundPath(paths, "test directory/.hidden symlink/2.txt", BPOL_mirror, n_star);
  checkFoundPath(paths, "test directory/.hidden symlink/3.txt", BPOL_mirror, n_star);
  checkFoundPath(paths, "test directory/bar-a.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/bar-b.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/empty-directory", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/foo 1", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/foo 1/bar", BPOL_mirror, NULL);
  assert_true(strTableGet(paths, str("test directory/foo 1/bar/1.txt")) == NULL);
  checkFoundPath(paths, "test directory/foo 1/bar/2.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/foo 1/bar/3.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-a.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-b.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/foo 1/test-file-c.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/foo 1/♞.☂", BPOL_mirror, NULL);
  assert_true(strTableGet(paths, str("test directory/foobar a1.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foobar a2.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foobar b1.txt")) == NULL);
  assert_true(strTableGet(paths, str("test directory/foobar b2.txt")) == NULL);
  checkFoundPath(paths, "test directory/symlink", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/φ.txt", BPOL_mirror, NULL);
  checkFoundPath(paths, "test directory/€.txt", BPOL_mirror, NULL);
  CR_RegionRelease(paths_region);

  /* Check ignore expressions. */
  checkIgnoreExpression(root, "test/data/.*(tmp|config-files|metadata)$", true);
  checkIgnoreExpression(root, "^never-matches-anything$", false);
  checkIgnoreExpression(root, "\\.hidden symlink/2\\.txt$", false);
  checkIgnoreExpression(root, "1\\.txt$", true);
  checkIgnoreExpression(root, "foobar", true);
}

int main(void)
{
  testGroupStart("simple file search");
  StringView cwd = getCwd();
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
