[![Build Status](https://travis-ci.org/AlxHnr/nano-backup.svg?branch=master)](https://travis-ci.org/AlxHnr/nano-backup)
[![codecov.io](https://codecov.io/github/AlxHnr/nano-backup/coverage.svg?branch=master)](https://codecov.io/github/AlxHnr/nano-backup?branch=master)

Nano-backup provides a precise way to track files. It was intended for
power-users who want to keep track of their fully customized Unix system.
It makes it easy to backup only the least amount of files required to
restore your system. Nano-backup does not try to replace existing backup
tools and focuses only on local backups. It stores full snapshots of files
and may not be suited for backing up large VM images.

## Installation

Building nano-backup requires a C99 compiler,
[pkg-config](http://www.freedesktop.org/wiki/Software/pkg-config/) and
[OpenSSL](https://www.openssl.org/). Clone this repository or download the
[latest release](https://github.com/AlxHnr/nano-backup/releases) and run
the following commands inside the project directory:

```sh
make
cp build/nb /usr/bin
```

**Note:** Gentoo users can install nano-backup directly from my
[overlay](https://github.com/AlxHnr/gentoo-overlay).

## Usage

To create a backup repository just create a new directory:

```sh
mkdir repo/
```

The repository can be configured via the file `config` inside it. Here is
an example for backing up two directories:

```
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

```
[ignore]
\.(pyc|class)$
^/home/user/.+~$
```

Regular expressions can also be used for matching files you want to backup.
Just prefix a pattern with an additional slash:

```
[copy]
/home/user//\.(png|jpg)$
```

**Note**: A regular expression can not contain a slash.

### Restoring files

To restore a file or directory, pass its full or relative path to
nano-backup:

```sh
nb repo/ 0 file.txt
```

The _0_ is the id of the latest backup. The backup before it would be _1_,
etc.

**Note**: The number is only relevant for tracked paths since only they
have a history.

## Policies

Policies specify how files should be backed up. They apply only to the last
element of a path:

```
[policy]
/home/user/last-element
```

Policy name | Description
------------|-------------
copy        | Backup only the latest version of a file.
mirror      | Like copy, but if a file gets removed from the system, it will also be removed from the backup.
track       | Keep a history of every change, including modification timestamps, owner, group and permission bits.
ignore      | Allows specifying regular expressions for excluding paths.

## Frequently asked questions

### How do i synchronize two repositories?

Here is an example for the repositories _old/_ and _current/_:

```sh
cp -rn current/* old/
cp current/{config,metadata} old/
nb old/ gc
```

### Why does it rely on timestamps and file sizes to check for changes?

If a files size and timestamp has not changed, it is assumed that its
content is still the same. While this method is extremely fast, it could
allow an attacker to tamper with files. But then again, an attacker with
file access could also tamper with the repository directly, or even replace
the nb binary. On an uncompromised system with a working clock this
shouldn't be an issue. Otherwise consider running some serious integrity
checker or [IDS](https://en.wikipedia.org/wiki/Intrusion_detection_system)
from a hardened live CD on an air-gapped system.

### Why does it still use SHA-1? It is broken!

This doesn't affect nano-backup. Any hash would do, as long as it can be
used to:

* _roughly_ estimate whether a file has changed
* generate unique filenames for the backup repository

Collisions are handled properly.

### Why don't use Git for backups?

Git can't backup empty directories, doesn't handle binary files well and
always keeps a history of everything. Sometimes it is desired to backup
only the latest version of a file/directory, or even discard them like a
mirror-style sync would do. Wiping a single file from a repository,
including its history and residue is trivial with nano-backup. Git also has
a completely different workflow and usually requires more commands to do a
backup.
