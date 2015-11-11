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
  Implements fundamental backup operations.
*/

#include "backup.h"

#include "search.h"

/** Initiates a backup by updating the given metadata with new or changed
  files found trough the specified search tree. To speed things up, hash
  computations of some files are skipped, which leaves the metadata in an
  incomplete state once this function returns. This allows to show a short
  summary of changes to the user as early as possible, before continuing
  with the backup.

  @param metadata A valid metadata struct containing informations about the
  latest backup. Once this function returns, the metadata will be left in
  an incomplete state and should never be passed to this function again.
  It should not be written to disk unless the backup gets completed,
  otherwise it may lead to a corrupted backup repository.
  @param root_node The search tree which will be used to search the file
  system. This function will modify the given tree as described in the
  documentation of searchNew().
*/
void initiateBackup(Metadata *metadata, SearchNode *root_node)
{
  (void)metadata;
  (void)root_node;
}
