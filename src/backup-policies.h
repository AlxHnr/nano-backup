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
  @file backup-policies.h Defines various policies for tracking files.
*/

#ifndef _NANO_BACKUP_BACKUP_POLICIES_H_
#define _NANO_BACKUP_BACKUP_POLICIES_H_

/** Contains a backup policy. */
typedef enum
{
  BPOL_none,
  BPOL_mirror,
  BPOL_collect,
  BPOL_track,
}BackupPolicy;

#endif
