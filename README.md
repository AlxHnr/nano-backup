[![travis](https://travis-ci.org/AlxHnr/nano-backup.svg?branch=master)](https://travis-ci.org/AlxHnr/nano-backup)
[![codecov](https://codecov.io/github/AlxHnr/nano-backup/coverage.svg?branch=master)](https://codecov.io/github/AlxHnr/nano-backup?branch=master)
[![license](https://img.shields.io/badge/license-MIT-brightgreen.svg)](https://github.com/AlxHnr/nano-backup/blob/master/LICENSE)
[![release](https://img.shields.io/badge/version-0.2.0-lightgrey.svg)](https://github.com/AlxHnr/nano-backup/releases/tag/v0.2.0)
[![overlay](https://img.shields.io/badge/gentoo-overlay-62548F.svg)](https://github.com/AlxHnr/gentoo-overlay)
![screenshot](https://cdn.rawgit.com/AlxHnr/nano-backup/1729b21e/screenshot.svg)

## Installation

Building nano-backup requires a C99 compiler,
[pkg-config](http://www.freedesktop.org/wiki/Software/pkg-config/) and
[OpenSSL](https://www.openssl.org/). Download the
[latest release](https://github.com/AlxHnr/nano-backup/releases/tag/v0.2.0)
and run the following commands inside the project directory:

```sh
make
cp build/nb /usr/bin
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

### How do i synchronize two repositories?

Here is an example for the repositories _old/_ and _current/_:

```sh
cp -rn current/* old/
cp current/{config,metadata} old/
nb old/ gc
```

### Why another backup tool?

There are many _good_ backup tools, implementing many _different_ backup
strategies. Nano-backup does not try to replace them and focuses only on
local backups. It stores full snapshots of files and may not be suited for
backing up large VM images.

Nano-backup shines in tracking stuff like system config files and binpkg
cache directories, without polluting the repository/history with stuff you
never want to restore again. It allows you to wipe files completely from
the repository by simply removing their entries from the config file. This
keeps the repository as lean as possible.

### Why don't use Git for backups?

Git can't backup empty directories, doesn't handle binary files well and
always keeps a history of everything. Sometimes it is desired to backup
only the latest version of a file/directory, or even discard them like a
mirror-style sync would do. Git usually requires more commands to do a
backup and provides only a very dull change summary in its staging area.
E.g. lack of explicit changes in file permissions, owner, etc.

### Why does it rely on timestamps and file sizes to check for changes?

If a files size and timestamp has not changed, it is assumed that its
content is still the same. While this method is extremely fast, it could
allow an attacker to tamper with files. But then again, an attacker with
file access could also tamper with the repository directly, or even replace
the nb binary. On an uncompromised system with a working clock this
shouldn't be an issue.

### Why does it still use SHA-1? It is broken!

Nano-backup doesn't even use SHA-1 for its cryptographic properties. It
uses it to _roughly_ estimate whether a file has changed or not. Hash
collisions are still handled properly. You don't need to worry if two
_distinct_ files have the same size and hash.
