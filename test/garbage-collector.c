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
  Tests functions for removing unreferenced files from the repository.
*/

#include "garbage-collector.h"

#include "test.h"
#include "test-common.h"
#include "path-builder.h"
#include "safe-wrappers.h"

/** Generates various dummy files in "tmp/repo". */
static void populateRepoWithDummyFiles(void)
{
  sMkdir("tmp/repo/a");
  sMkdir("tmp/repo/a/b");
  sMkdir("tmp/repo/a/c");
  sMkdir("tmp/repo/a/c/d");
  sMkdir("tmp/repo/a/1");
  sMkdir("tmp/repo/a/2");
  sMkdir("tmp/repo/a/3");
  sMkdir("tmp/repo/a/3/4");
  sMkdir("tmp/repo/a/3/5");
  sMkdir("tmp/repo/a/3/6");
  sFclose(sFopenWrite("tmp/repo/a/b/foo"));
  sFclose(sFopenWrite("tmp/repo/a/c/bar"));
  sFclose(sFopenWrite("tmp/repo/a/c/d/backup"));
  sFclose(sFopenWrite("tmp/repo/a/3/nano"));
  sFclose(sFopenWrite("tmp/repo/a/3/5/this"));
  sFclose(sFopenWrite("tmp/repo/a/3/5/is"));
  sFclose(sFopenWrite("tmp/repo/a/3/5/a"));
  sFclose(sFopenWrite("tmp/repo/a/3/5/test"));
  sSymlink("../file.txt", "tmp/repo/file.txt");
  sSymlink("foo", "tmp/repo/a/b/bar");
  sSymlink("bar", "tmp/repo/a/c/q");
  sSymlink("../../../a", "tmp/repo/a/3/6/link-1");
  sSymlink("../../../../repo", "tmp/repo/a/3/6/link-2");
  sSymlink("../../../../file.txt", "tmp/repo/a/3/6/link-3");
  sSymlink("non-existing", "tmp/repo/a/2/broken");
}

/** Wrapper around collectGarbage() which checks the returned stats. */
static void testCollectGarbage(Metadata *metadata, const char *repo_path,
                               size_t count, uint64_t size)
{
  GCStats stats = collectGarbage(metadata, repo_path);

  assert_true(stats.count == count);
  assert_true(stats.size == size);
}

int main(void)
{
  testGroupStart("symlink handling");
  sMkdir("tmp/repo");
  sFclose(sFopenWrite("tmp/file.txt"));
  Metadata *empty_metadata = metadataNew();

  /* Repository is simple directory. */
  populateRepoWithDummyFiles();
  testCollectGarbage(empty_metadata, "tmp/repo", 25, 0);
  assert_true(countItemsInDir("tmp/repo") == 0);
  assert_true(sPathExists("tmp/file.txt"));

  /* Repository is symlink to directory. */
  populateRepoWithDummyFiles();
  sSymlink("repo", "tmp/link-to-repo");
  testCollectGarbage(empty_metadata, "tmp/link-to-repo", 25, 0);
  assert_true(countItemsInDir("tmp/repo") == 0);
  assert_true(sPathExists("tmp/link-to-repo"));
  assert_true(sPathExists("tmp/file.txt"));

  /* Repository is symlink to file. */
  sRemove("tmp/link-to-repo");
  sSymlink("file.txt", "tmp/link-to-repo");
  populateRepoWithDummyFiles();
  testCollectGarbage(empty_metadata, "tmp/link-to-repo", 0, 0);
  assert_true(countItemsInDir("tmp/repo") == 25);
  assert_true(sPathExists("tmp/link-to-repo"));
  assert_true(sPathExists("tmp/file.txt"));

  /* Repository is broken symlink. */
  sRemove("tmp/link-to-repo");
  sSymlink("non-existing", "tmp/link-to-repo");

  assert_error(collectGarbage(empty_metadata, "tmp/link-to-repo"),
               "failed to access \"tmp/link-to-repo\": No such file or directory")

  assert_true(countItemsInDir("tmp/repo") == 25);
  assert_true(sPathExists("tmp/link-to-repo"));
  assert_true(sPathExists("tmp/file.txt"));
  testGroupEnd();
}
