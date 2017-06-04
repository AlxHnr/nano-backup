/** @file
  Defines various policies for tracking files.
*/

#ifndef NANO_BACKUP_BACKUP_POLICIES_H
#define NANO_BACKUP_BACKUP_POLICIES_H

/** Defines different backup policies. */
typedef enum
{
  /** This policy applies only to unspecified parent directories of a
    backed up file. They are required to restore full paths to files if
    they don't exist already. These directories will be backed up silently
    without the users knowledge.*/
  BPOL_none,

  /** Backups only the latest version of a file. */
  BPOL_copy,

  /** Like BPOL_copy, but if a file gets removed from the system, it will
    also be removed from the backup. */
  BPOL_mirror,

  /** Backup every change of a file. This includes metadata like the
    timestamp, the files owner and its permission bits. */
  BPOL_track,

  /** Ignore the file. This is only a helper policy for parsing config
    files. */
  BPOL_ignore,
}BackupPolicy;

#endif
