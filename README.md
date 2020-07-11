[![travis](https://travis-ci.org/AlxHnr/nano-backup.svg?branch=master)](https://travis-ci.org/AlxHnr/nano-backup)
[![codecov](https://codecov.io/github/AlxHnr/nano-backup/coverage.svg?branch=master)](https://codecov.io/github/AlxHnr/nano-backup?branch=master)
[![license](https://img.shields.io/badge/license-MIT-brightgreen.svg)](https://github.com/AlxHnr/nano-backup/blob/master/LICENSE)
![screenshot](https://cdn.rawgit.com/AlxHnr/nano-backup/1729b21e/screenshot.svg)

* **Precise** - Displays a good, concise summary about _what_ has changed
  on your system
* **Full control** - Never sneaks in unapproved changes
* **Atomic** - If it crashes, the backup will be like it was before
* **Few dependencies** - Only needs a C Compiler and GNU Make
* **Portable** - Conforms strictly to C99 and POSIX.1-2001
* **Fast and snappy** - Reduces friction and is satisfying to use

## Why?

I needed something to track files precisely. Without polluting the change
history with stuff I never want to restore again. Tools like git don't
handle binary files well and always keep a history of everything. This
makes it useless for for backing up binpkg cache directories. I also want
to know exactly _what_ has changed on my system since the last backup. This
includes file permissions, timestamps, etc. All that in a simple,
straightforward and atomic way, preventing changes to sneak in without
approval.

I _greatly_ disagree with the idea to just `dd` or `rsync` the entire
hard-drive. Sometimes you may tinker around with config files you never
need again, probably leaving them in an inconsistent state. There is no
reason to have those files polluting the backup history. Nano-backup makes
it easy to track only the few files you really need. To wipe files
completely from the repository, simply remove their entries from the config
file. This keeps the backup as lean as possible.

**Note**: Nano-backup does not try to replace existing backup tools and
implements only a handful of backup strategies. It focuses on local backups
and stores full snapshots of files. This makes it unsuitable for backing up
large VM images.

## Installation

Clone this repository and run the following command inside the project
directory:

```sh
make && cp build/nb /usr/bin
```

## Usage

To create a backup repository just create a new directory:

```sh
mkdir repo/
```

The repository can be configured via the file `config` inside it. Here is
an example for backing up two directories:

```desktop
[copy]
/home/user/Videos
/home/user/Pictures
```

The first line sets the copy [policy](#policies). The other lines are
absolute paths to files or directories which should be backed up. To do a
backup, pass the repository to nano-backup:

```sh
nb repo/
```

To prevent files from being backed up, set the ignore policy. This allows
specifying regular expressions, which will be matched against full,
absolute filepaths. They must be valid POSIX extended regular expressions:

```desktop
[ignore]
\.(pyc|class)$
^/home/user/.+~$
```

Regular expressions can also be used for matching files you want to backup.
Just prefix a pattern with an additional slash:

```desktop
[copy]
/home/user//\.(png|jpg)$
/home//^(foo|bar)$/.bashrc
```

**Note**: This expression will not match recursively and can be terminated
by a slash.

### Restoring files

To restore a file or directory, pass its full or relative path to
nano-backup:

```sh
nb repo/ 0 file.txt
```

The _0_ is the id of the latest backup. The backup before it would be _1_,
etc.

**Note**: This number will be ignored for copied/mirrored files, which will
always be restored to their latest state.

## Policies

Policies specify how files should be backed up. They apply only to the last
element of a path:

```desktop
[policy]
/home/user/last-element
```

Policy name | Description
------------|-------------
copy        | Backup only the latest version of a file.
mirror      | Like copy, but if a file gets removed from the system, it will also be removed from the backup.
track       | Keep a full history of every change.
ignore      | Allows specifying regular expressions for excluding paths.

## Frequently asked questions

### How do I synchronize two repositories?

Here is an example for the repositories _old/_ and _current/_:

```sh
cp -rn current/* old/
cp current/{config,metadata} old/
nb old/ gc
```

### Can I run a hook before/after each backup?

No. Nano-backup tries to be as minimal as possible. The recommended way is
to write a wrapper:

```sh
#/bin/sh -e

... # Run stuff before the backup.

nb "$HOME/backup"

... # Run stuff after the backup.
```

### How do I tell it to do automatic backups once a day?

You can't. This is the task of tools like cron. But doing so would
completely defeat the purpose of using nano-backup in the first place.
Nano-backup was written for precise and explicit backups, providing full
control over changes. If you still want to go the cron-way, make sure to
log the output to get a useful changelog.

### Does it support encrypted backups?

No. There are many filesystems solving this problem, some even support
distributed cloud storage. Use them for housing your repository.

### Does it deduplicate data chunks of variable length?

No, it does only whole-file deduplication. Implementing such a feature
_properly_ as a single developer with very limited time is not possible.
The cost greatly exceeds the benefit, especially in times of modern
deduplicating filesystems.

### Does it compress the backed up files?

No. It would make the codebase more complicated, may introduce additional
dependencies and would take quite some time to test properly.

### Why does it rely on timestamps and file sizes to check for changes?

If a files size and timestamp has not changed, it is assumed that its
content is still the same. While this method is extremely fast, it could
allow an attacker to tamper with files. But then again, an attacker with
file access could also tamper with the repository directly, or even replace
the nb binary. On an uncompromised system with a working clock this
shouldn't be an issue.
