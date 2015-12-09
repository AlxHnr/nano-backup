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
  Tests the core backup logic.
*/

#include "backup.h"

#include "test.h"
#include "metadata.h"
#include "search-tree.h"
#include "test-common.h"
#include "error-handling.h"

/** Finds the node that represents the directory in which this test runs.
  It will terminate the program if the node doesn't exist, or its parent
  nodes are invalid.

  @param metadata The metadata containing the nodes.
  @param cwd The current working directory.

  @return The found node.
*/
static PathNode *findCwdNode(Metadata *metadata, String cwd)
{
  for(PathNode *node = metadata->paths; node != NULL; node = node->subnodes)
  {
    if(node->policy != BPOL_none)
    {
      die("path shouldn't have a policy: \"%s\"", node->path.str);
    }
    else if(node->history->next != NULL)
    {
      die("path has too many history points: \"%s\"", node->path.str);
    }
    else if(node->next != NULL)
    {
      die("item is not the last in list: \"%s\"", node->path.str);
    }
    else if(node->history->state.type != PST_directory)
    {
      die("not a directory: \"%s\"", node->path.str);
    }
    else if(strCompare(node->path, cwd))
    {
      return node;
    }
  }

  die("path does not exist in metadata: \"%s\"", cwd.str);
  return NULL;
}

/** Counts the path elements in the given string. E.g. "/home/foo/bar" has
  3 path elements.

  @param string The string containing the paths.

  @return The path element count.
*/
static size_t countPathElements(String string)
{
  size_t count = 0;

  for(size_t index = 0; index < string.length; index++)
  {
    count += string.str[index] == '/';
  }

  return count;
}

int main(void)
{
  testGroupStart("discovering new files");
  String cwd = getCwd();
  volatile size_t cwd_depth = countPathElements(cwd);

  Metadata *metadata = metadataNew();
  SearchNode *root_node = searchTreeLoad("generated-config-files/backup-search-test.txt");

  initiateBackup(metadata, root_node);

  assert_true(metadata->current_backup.ref_count == cwd_depth + 10);

  findCwdNode(metadata, cwd);
  testGroupEnd();
}
