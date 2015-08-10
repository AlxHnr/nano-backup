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
  @file search.c Tests the filesystem search implementation.
*/

#include "search.h"

#include "test.h"
#include "string-table.h"
#include "safe-wrappers.h"
#include "error-handling.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

/** Determines the current working directory.

  @return A pointer to an allocated string that must be freed by the
  caller.
*/
static String getCwd(void)
{
  int old_errno = errno;
  size_t capacity = 128;
  char *buffer = sMalloc(capacity);
  char *result = NULL;

  do
  {
    result = getcwd(buffer, capacity);
    if(result == NULL)
    {
      if(errno != ERANGE)
      {
        dieErrno("failed to get current working directory");
      }

      capacity = sSizeMul(capacity, 2);
      buffer = sRealloc(buffer, capacity);
      errno = old_errno;
    }
  }while(result == NULL);

  String cwd = strCopy(str(buffer));
  free(buffer);

  return cwd;
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
    else if(strCompare(result.path, cwd))
    {
      break;
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
  policy. The paths will only contain the part after the given cwd.
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
      file_count += (result.type == SRT_regular ||
                     result.type == SRT_symlink);
      recursion_depth += result.type == SRT_directory;
      strtableMap(table, trimCwd(result.path, cwd), (void *)result.policy);
    }
  }

  return file_count;
}

/** Asserts that the given table contains a mapping of the given path to
  the given policy. */
static void checkHasPolicy(StringTable *table, const char *path,
                           BackupPolicy policy)
{
  assert_true(strtableGet(table, str(path)) == (void *)policy);
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
  StringTable *found_files = strtableNew(0);
  assert_true(populateDirectoryTable(context, found_files, cwd) == 37);
  finishSearch(context, cwd_depth);

  checkHasPolicy(found_files, "empty.txt",   BPOL_track);
  checkHasPolicy(found_files, "example.txt", BPOL_track);
  checkHasPolicy(found_files, "symlink.txt", BPOL_track);

  checkHasPolicy(found_files, "valid-config-files",                              BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/ignore-patterns-only-1.txt",   BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/ignore-patterns-only-2.txt",   BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/inheritance-1.txt",            BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/inheritance-2.txt",            BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/inheritance-3.txt",            BPOL_track);
  checkHasPolicy(found_files, "valid-config-files/no-paths-and-no-ignores.txt",  BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/root-with-regex-subnodes.txt", BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/simple-BOM-noeol.txt",         BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/simple-BOM.txt",               BPOL_mirror);
  checkHasPolicy(found_files, "valid-config-files/simple-noeol.txt",             BPOL_copy);
  checkHasPolicy(found_files, "valid-config-files/simple.txt",                   BPOL_copy);

  checkHasPolicy(found_files, "broken-config-files",                               BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/BOM-simple-error.txt",          BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/closing-brace-empty.txt",       BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/closing-brace.txt",             BPOL_track);
  checkHasPolicy(found_files, "broken-config-files/empty-policy-name.txt",         BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/invalid-ignore-expression.txt", BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/invalid-path-1.txt",            BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/invalid-path-2.txt",            BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/invalid-path-3.txt",            BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/invalid-policy.txt",            BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/invalid-regex.txt",             BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/multiple-errors.txt",           BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/opening-brace-empty.txt",       BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/opening-brace.txt",             BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/pattern-without-policy.txt",    BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/redefine-1.txt",                BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/redefine-2.txt",                BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/redefine-3.txt",                BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/redefine-policy-1.txt",         BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/redefine-policy-2.txt",         BPOL_copy);
  checkHasPolicy(found_files, "broken-config-files/redefine-root-1.txt",           BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/redefine-root-2.txt",           BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/redefine-root-policy-1.txt",    BPOL_mirror);
  checkHasPolicy(found_files, "broken-config-files/redefine-root-policy-2.txt",    BPOL_mirror);

  assert_true(strtableGet(found_files, str("template-config-files"))  == NULL);
  assert_true(strtableGet(found_files, str("generated-config-files")) == NULL);
  strtableFree(found_files);

  SearchNode *node = checkCwdTree(root, cwd_depth);
  assert_true(node != NULL);
}

int main(void)
{
  testGroupStart("simple file search");
  String cwd = getCwd();

  testSimpleSearch(cwd);
  testGroupEnd();
}
